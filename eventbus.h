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
    std::vector<std::string> topics; // на что подписан
    bool active = true;
};

class EventBus {
public:
    static bool init(Config*);
    static void stop();

    // НОВОЕ: принять СЫРОЙ JSON пуheг-сообщения целиком.
    // Сама определяет топики по ключам верхнего уровня
    // ("transport", "process", "radio", "addressbook", ...)
    // и рассылает сообщение подписчикам с совпавшим топиком.
    static void emit(const std::string& rawJson);

    // Подписки
    static void subscribe(uint32_t ip, uint16_t port);
    static void unsubscribe(uint32_t ip, uint16_t port);


private:
    static void readerLoop();

    static int sock_;
    static std::thread thr_;
    static std::atomic<bool> running_;
    static std::vector<EventSubscriber> msgSubscribers;
    static std::mutex mtx_;
    // НОВОЕ: буфер приёма, выделяется один раз при init()
    static std::vector<char> recvBuf_;
};

#endif // EVENTBUS_H


