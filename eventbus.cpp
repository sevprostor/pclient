#include "eventbus.h"
#include "commander.h"
#include "log.h"
#include "config.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <cstring>
#include <ctime>

int EventBus::sock_ = -1;
std::thread EventBus::thr_;
std::atomic<bool> EventBus::running_{false};
std::vector<EventSubscriber> EventBus::subs_;
std::mutex EventBus::mtx_;

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

        if (text == "subscribe") {
            subscribe(from.sin_addr.s_addr, ntohs(from.sin_port));
        } else if (text == "unsubscribe") {
            unsubscribe(from.sin_addr.s_addr, ntohs(from.sin_port));
        } else if (Commander::isCommand(text)) {
            // локальный управляющий вход, source = "local"
            Commander::handle(text.substr(Commander::PREFIX_LEN), "local");
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
