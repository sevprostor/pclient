#pragma once
#include <cstdint>
#include <vector>
#include <functional>
#include <string>


class Transport {
public:
    using OnFrameCallback = std::function<void(const std::vector<uint8_t>&)>;

    bool init(uint16_t driverPort);
    void stop();
    // Принимаем IP-адрес строкой, драйвер сам разберётся с MAC
    bool sendFrame(const std::string& destIp, const std::vector<uint8_t>& payload);
    void poll(int timeoutMs = 100);
    void setOnFrame(OnFrameCallback cb) { onFrame_ = cb; }

private:
    int sock_ = -1;
    uint16_t driverPort_ = 9400;
    OnFrameCallback onFrame_;
};
