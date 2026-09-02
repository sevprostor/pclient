#ifndef EVENTBUS_H
#define EVENTBUS_H

#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstdint>
#include "config.h"

struct EventSubscriber {
    uint32_t ip;      // сетевой порядок
    uint16_t port;
};

class EventBus {
public:
    static bool init(Config*);       // сокет 127.0.0.1:port + поток чтения
    static void stop();

    // толкнуть событие всем подписчикам; fields — готовые JSON-пары: "\"id\":5"
    static void emit(const std::string& type, const std::string& fields = "");

    static void subscribe(uint32_t ip, uint16_t port);
    static void unsubscribe(uint32_t ip, uint16_t port);

    static void setTransferSubscriber(uint32_t ip, uint16_t port);
    static void emitTransfer(const std::vector<uint8_t>& data);
    static bool hasTransferSubscriber();

private:
    static void readerLoop();

    static int sock_;
    static std::thread thr_;
    static std::atomic<bool> running_;
    static std::vector<EventSubscriber> subs_;
    static std::mutex mtx_;

    static EventSubscriber transferSub_;
    static std::atomic<bool> transferSubActive_;
};

#endif // EVENTBUS_H
