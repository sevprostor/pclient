#include "eventbus.h"
#include "commander.h"
#include "config.h"
#include "log.h"
#include "config.h"
#include "addressbook.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <cstring>
#include <ctime>
#include <vector>
#include <cerrno> // для errno и EADDRINUSE

int EventBus::sock_ = -1;
std::thread EventBus::thr_;
std::atomic<bool> EventBus::running_{false};
std::vector<EventSubscriber> EventBus::subs_;
std::mutex EventBus::mtx_;

EventSubscriber EventBus::transferSub_ = {0, 0};
std::atomic<bool> EventBus::transferSubActive_{false};

//bool EventBus::init(uint16_t startPort) {
bool EventBus::init(Config* config) {
    uint16_t currentPort = config->eventBusPort;
    const uint16_t maxAttempts = 100; // Защита от бесконечного цикла

    for (uint16_t attempt = 0; attempt < maxAttempts; ++attempt) {
        sock_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock_ < 0) {
            Log::error("EventBus", "Не удалось создать сокет");
            return false;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(currentPort);

        if (bind(sock_, (sockaddr*)&addr, sizeof(addr)) == 0) {
            // Успешный бинд!
            Log::info("EventBus", "Слушаю 127.0.0.1:", currentPort);
            running_ = true;
            thr_ = std::thread(readerLoop);
            config->eventBusPort = currentPort;
            return true;
        }

        // Бинд не удался
        if (errno == EADDRINUSE) {
            Log::warn("EventBus", "Порт ", currentPort, " занят, пробую ", currentPort + 1);
            ::close(sock_);
            sock_ = -1;
            currentPort++;
        } else {
            // Фатальная ошибка (не "порт занят", а что-то серьёзнее)
            Log::error("EventBus", "Ошибка bind на порту ", currentPort, ": ", strerror(errno));
            ::close(sock_);
            sock_ = -1;
            return false;
        }
    }

    Log::error("EventBus", "Не удалось найти свободный порт после ", maxAttempts, " попыток");
    return false;
}

/*
bool EventBus::init(uint16_t port) {
    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) return false;

    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // только localhost
    a.sin_port = htons(port);
    if (bind(sock_, (sockaddr*)&a, sizeof(a)) < 0) {
        Log::error("EventBus", "bind failed, port ", port);
        return false;
    }

    running_ = true;
    thr_ = std::thread(readerLoop);
    Log::info("EventBus", "Слушаю 127.0.0.1:", port);
    return true;
}*/

void EventBus::stop() {
    running_ = false;
    if (thr_.joinable()) thr_.join();
    if (sock_ >= 0) ::close(sock_);
}

void EventBus::readerLoop() {
    char buf[2048];
    while (running_) {
        fd_set rfds; FD_ZERO(&rfds); FD_SET(sock_, &rfds);
        timeval tv{0, 100000};
        int r = select(sock_ + 1, &rfds, nullptr, nullptr, &tv);
        if (r <= 0) continue;

        sockaddr_in from{}; socklen_t fl = sizeof(from);
        ssize_t n = recvfrom(sock_, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fl);
        if (n <= 0) continue;
        buf[n] = '\0';
        std::string text(buf);

        if (text == "subscribe") {
            subscribe(from.sin_addr.s_addr, ntohs(from.sin_port));
        } else if (text == "unsubscribe") {
            unsubscribe(from.sin_addr.s_addr, ntohs(from.sin_port));
        } else if (Commander::isCommand(text)) {
            // локальный управляющий вход, source = "local"
            //Commander::handle(text.substr(Commander::PREFIX_LEN), "local");
            commander.handle(text.substr(Commander::PREFIX_LEN), "local");
        }

        // В EventBus::readerLoop(), блок обработки бинарных пакетов:

        // 3. БИНАРНЫЙ РЕЖИМ для pscp!
        // Формат: [4 байта: destIp (network order)] [N байт: payload, начиная с FH\x01]
        if (n >= 7) { // минимум 4 байта IP + 3 байта магика FH\x01
            // Читаем IP из первых 4 байт
            uint32_t ipNetworkOrder;
            std::memcpy(&ipNetworkOrder, buf, 4);

            char ipStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &ipNetworkOrder, ipStr, INET_ADDRSTRLEN);

            std::vector<uint8_t> payload(buf + 4, buf + n);

            // Проверяем магик FH
            if (payload.size() >= 3 && payload[0] == 'F' && payload[1] == 'H' && payload[2] == '\x01') {
                // Ищем контакт в адресной книге
                Addressbook::Contact destContact;
                if (Addressbook::getInstance().findContactByIp(ipStr, destContact)) {
                    MsgParser::PuhegUpperMessage pumsg;
                    pumsg.what = "toss";
                    pumsg.howmuch = destContact.id;
                    pumsg.msg = parser.encodeMsg(payload);
                    parser.packMessage(&pumsg);

                    wsclient.sendMessage(&pumsg, false);
                    Log::info("EventBus", "Binary toss -> IP:", ipStr, " (ID:", destContact.id, ") size:", payload.size(), " bytes");
                } else {
                    Log::error("EventBus", "Binary toss -> IP:", ipStr, " не найден в адресной книге");
                }
            } else {
                Log::warn("EventBus", "Получен бинарный пакет, но без FH-магика. Игнорируется.");
            }
            continue;
        }
    }
}

void EventBus::subscribe(uint32_t ip, uint16_t port) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& s : subs_)
        if (s.ip == ip && s.port == port) return;
    subs_.push_back({ip, port});
    Log::info("EventBus", "Новый подписчик, всего: ", subs_.size());
}

void EventBus::unsubscribe(uint32_t ip, uint16_t port) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto it = subs_.begin(); it != subs_.end(); ++it)
        if (it->ip == ip && it->port == port) { subs_.erase(it); break; }
}

void EventBus::emit(const std::string& type, const std::string& fields) {
    std::string msg = "{\"ts\":" + std::to_string((long long)time(nullptr))
                      + ",\"type\":\"" + type + "\"";
    if (!fields.empty()) msg += "," + fields;
    msg += "}";

    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& s : subs_) {
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_addr.s_addr = s.ip;
        to.sin_port = htons(s.port);
        sendto(sock_, msg.data(), msg.size(), 0, (sockaddr*)&to, sizeof(to));
    }
}
