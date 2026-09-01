#ifndef EVENTS_H
#define EVENTS_H
#pragma once
#include <thread>
#include <atomic>
#include <cstdint>

class EventBusClient {
public:
    bool start(uint16_t busPort = 9400);
    void stop();
private:
    int sock_ = -1;
    std::thread thr_;
    std::atomic<bool> running_{false};
};
#endif // EVENTS_H
