#include "eventbus.h"
#include "commander.h"
#include "TCPclient.h"
#include "msgparser.h"
#include "addressbook.h"
#include "log.h"
#include "config.h"
#include "libs/json.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <cstring>
#include <ctime>
#include <vector>
#include <cerrno>
//#include <sstream>

// Объявления глобальных объектов из main.cpp
extern TCPclient wsclient;
extern Commander commander;
extern MsgParser parser;

using json = nlohmann::json; // удобное короткое имя

// Объявления глобальных объектов из main.cpp
extern TCPclient wsclient;
extern Commander commander;
extern MsgParser parser;

int EventBus::sock_ = -1;
std::thread EventBus::thr_;
std::atomic<bool> EventBus::running_{false};
std::vector<EventSubscriber> EventBus::msgSubscribers;
std::mutex EventBus::mtx_;

bool EventBus::init(Config* config) {
    uint16_t currentPort = config->eventBusPort;
    const uint16_t maxAttempts = 100;

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
            Log::info("EventBus", "Слушаю 127.0.0.1:", currentPort);
            running_ = true;
            thr_ = std::thread(readerLoop);
            config->eventBusPort = currentPort;
            return true;
        }

        if (errno == EADDRINUSE) {
            Log::warn("EventBus", "Порт ", currentPort, " занят, пробую ", currentPort + 1);
            ::close(sock_);
            sock_ = -1;
            currentPort++;
        } else {
            Log::error("EventBus", "Ошибка bind на порту ", currentPort, ": ", strerror(errno));
            ::close(sock_);
            sock_ = -1;
            return false;
        }
    }

    Log::error("EventBus", "Не удалось найти свободный порт после ", maxAttempts, " попыток");
    return false;
}

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

        // ============================================================
        // ВАРИАНТ 1: JSON-запрос (начинается с '{')
        // Форматы: {"subscribe": ["topic1", "topic2", "haul"]}
        //          {"unsubscribe": true}
        // ============================================================
        if (n > 2 && buf[0] == '{') {
            // Строка точной длины n (бинарно-безопасно)
            std::string text(reinterpret_cast<const char*>(buf), n);

            // Парсим JSON; при ошибке ловим исключение и логируем позицию
            json j;
            try {
                j = json::parse(text);
            } catch (const json::parse_error& e) {
                Log::error("EventBus", "❌ Ошибка парсинга JSON (байт ", e.byte, "): ",
                           e.what(), " | текст: ", text);
                continue;
            }

            // ШАГ 1: запрос на подписку — ключ "subscribe" со значением-массивом
            if (j.contains("subscribe") && j["subscribe"].is_array()) {
                // Извлекаем только строковые элементы массива
                std::vector<std::string> topics;
                for (const auto& t : j["subscribe"]) {
                    if (t.is_string()) {
                        topics.push_back(t.get<std::string>());
                    }
                }

                if (!topics.empty()) {
                    // Регистрируем подписчика (если его ещё нет)
                    subscribe(from.sin_addr.s_addr, ntohs(from.sin_port));

                    // Обновляем список топиков у существующей записи
                    std::lock_guard<std::mutex> lk(mtx_);
                    for (auto& sub : msgSubscribers) {
                        if (sub.ip == from.sin_addr.s_addr && sub.port == ntohs(from.sin_port)) {
                            sub.topics = topics;
                            sub.active = true;
                            break;
                        }
                    }

                    // Логируем итоговый список топиков
                    std::string topicsStr;
                    for (const auto& t : topics) {
                        if (!topicsStr.empty()) topicsStr += ", ";
                        topicsStr += t;
                    }
                    Log::info("EventBus", "✅ Подписка на порту ", ntohs(from.sin_port),
                              ", топики: [", topicsStr, "]");
                } else {
                    Log::warn("EventBus", "⚠️ Ключ \"subscribe\" есть, но массив топиков пуст: ", text);
                }
                continue;
            }

            // ШАГ 2: запрос на отписку
            if (j.contains("unsubscribe")) {
                unsubscribe(from.sin_addr.s_addr, ntohs(from.sin_port));
                Log::info("EventBus", "✅ Отписка на порту ", ntohs(from.sin_port));
                continue;
            }

            // ШАГ 3: валидный JSON, но неизвестный запрос
            Log::warn("EventBus", "⚠️ Неизвестный JSON-запрос: ", text);
            continue;
        }

        // ============================================================
        // ВАРИАНТ 2: текстовая команда (>>> ...)
        // ============================================================
        if (Commander::isCommand(text)) {
            commander.handle(text.substr(Commander::PREFIX_LEN), "local");
            continue;
        }

        // ============================================================
        // ВАРИАНТ 3: бинарный PW-пакет
        // ============================================================
        if (n >= 7) {
            uint32_t ipNetworkOrder;
            std::memcpy(&ipNetworkOrder, buf, 4);

            char ipStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &ipNetworkOrder, ipStr, INET_ADDRSTRLEN);

            std::vector<uint8_t> payload(buf + 4, buf + n);

            if (payload.size() >= 3 && payload[0] == 'P' && payload[1] == 'W' && payload[2] == '\x01') {
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
                Log::warn("EventBus", "Получен бинарный пакет, но без PW-магика. Игнорируется.");
            }
            continue;
        }
    }
}

void EventBus::subscribe(uint32_t ip, uint16_t port) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& s : msgSubscribers)
        if (s.ip == ip && s.port == port) return;

    EventSubscriber sub;
    sub.ip = ip;
    sub.port = port;
    sub.active = true;
    msgSubscribers.push_back(sub);
    Log::info("EventBus", "Новый подписчик, всего: ", msgSubscribers.size());
}

void EventBus::unsubscribe(uint32_t ip, uint16_t port) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto it = msgSubscribers.begin(); it != msgSubscribers.end(); ++it)
        if (it->ip == ip && it->port == port) {
            msgSubscribers.erase(it);
            break;
        }
}


// ============================================================================
// Маршрутизация готового puheg-сообщения по подписчикам.
//
// 1. Парсим JSON, добавляем метку времени.
// 2. Проверяем, есть ли в transport.downlink.packet массив байт с магиком PW\x01.
//    Если да — это кадр Puheg Words Protocol: шлём бинарные данные подписчикам
//    с топиком "words" (отдельно от JSON-событий).
// 3. Остальное рассылаем как JSON-событие подписчикам с совпавшим топиком.
// ============================================================================

// Hex-кодирование байт для JSON-представления PW-кадра
static std::string bytesToHex(const std::vector<uint8_t>& data) {
    static const char hexChars[] = "0123456789abcdef";
    std::string res;
    res.reserve(data.size() * 2);
    for (uint8_t b : data) {
        res.push_back(hexChars[b >> 4]);
        res.push_back(hexChars[b & 0x0F]);
    }
    return res;
}

void EventBus::emit(const std::string& rawJson) {
    // ШАГ 1: парсим

    json j;
    try {
        j = json::parse(rawJson);
    } catch (const json::parse_error& e) {
        Log::error("EventBus", "emit: ошибка парсинга JSON: ", e.what());
        return;
    }
    if (!j.is_object()) return;

    // ШАГ 2: метка времени
    j["ts"] = static_cast<long long>(time(nullptr));

    // ШАГ 3: извлекаем PW-кадр из transport.downlink.packet
    // и кладём его ОТДЕЛЬНЫМ ключом верхнего уровня "words" (в JSON-виде)
    if (j.contains("transport") && j["transport"].is_object()) {
        const auto& tr = j["transport"];
        if (tr.contains("downlink") && tr["downlink"].is_object()) {
            const auto& dl = tr["downlink"];
            if (dl.contains("packet") && dl["packet"].is_array()) {
                std::vector<uint8_t> bytes;
                for (const auto& b : dl["packet"]) {
                    if (b.is_number_integer())
                        bytes.push_back(static_cast<uint8_t>(b.get<int>()));
                }
                // Магик PW\x01
                if (bytes.size() >= 3 && bytes[0]=='P' && bytes[1]=='W' && bytes[2]==0x01) {
                    //Log::info("EventBus", "PW");
                    j["words"] = json{
                        {"size",    bytes.size()},
                        {"payload", bytesToHex(bytes)}  // hex-строка
                    };

                    //Log::info("EventBus", "PW: ", bytesToHex(bytes));
                }
            }
        }
    }

    const std::string out = j.dump();

    // ШАГ 4: единообразная маршрутизация по ключам верхнего уровня
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& s : msgSubscribers) {
        if (!s.active) continue;
        for (const auto& topic : s.topics) {
            if (topic == "*" || j.contains(topic)) {
                //Log::info("EventBus", "raw packet to ", s.port, ": ", out);
                sockaddr_in to{};
                to.sin_family = AF_INET;
                to.sin_addr.s_addr = s.ip;
                to.sin_port = htons(s.port);
                sendto(sock_, out.data(), out.size(), 0, (sockaddr*)&to, sizeof(to));
                break; // одно сообщение — одна отправка подписчику
            }
        }
    }
}
