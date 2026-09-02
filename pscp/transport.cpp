#include "transport.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <cstdio>

bool Transport::init(uint16_t driverPort) {
    driverPort_ = driverPort;
    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(sock_, (sockaddr*)&addr, sizeof(addr)) < 0) return false;

    std::string sub = "{\"cmd\":\"subscribe\",\"events\":[\"transfer\"],\"port\":0}";
    sockaddr_in bus{};
    bus.sin_family = AF_INET;
    bus.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bus.sin_port = htons(driverPort_);
    sendto(sock_, sub.c_str(), sub.size(), 0, (sockaddr*)&bus, sizeof(bus));

    return true;
}

void Transport::stop() {
    if (sock_ >= 0) { ::close(sock_); sock_ = -1; }
}

bool Transport::sendFrame(const std::string& destIp, const std::vector<uint8_t>& payload) {
    // Конвертируем IP в 4 байта network order
    in_addr addr;
    if (inet_pton(AF_INET, destIp.c_str(), &addr) != 1) {
        fprintf(stderr, "Ошибка: неверный IP-адрес %s\n", destIp.c_str());
        return false;
    }

    // Формируем пакет: [4 байта: IP network order] + [payload]
    std::vector<uint8_t> packet;
    packet.reserve(4 + payload.size());

    const uint8_t* ipBytes = reinterpret_cast<const uint8_t*>(&addr.s_addr);
    packet.insert(packet.end(), ipBytes, ipBytes + 4);
    packet.insert(packet.end(), payload.begin(), payload.end());

    sockaddr_in bus{};
    bus.sin_family = AF_INET;
    bus.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bus.sin_port = htons(driverPort_);

    ssize_t n = sendto(sock_, packet.data(), packet.size(), 0, (sockaddr*)&bus, sizeof(bus));
    return n == static_cast<ssize_t>(packet.size());
}

void Transport::poll(int timeoutMs) {
    if (sock_ < 0) return;
    fd_set rfds; FD_ZERO(&rfds); FD_SET(sock_, &rfds);
    timeval tv{0, timeoutMs * 1000};
    if (select(sock_ + 1, &rfds, nullptr, nullptr, &tv) <= 0) return;

    uint8_t buf[2048];
    ssize_t n = recvfrom(sock_, buf, sizeof(buf), 0, nullptr, nullptr);
    if (n > 0 && onFrame_) {
        onFrame_(std::vector<uint8_t>(buf, buf + n));
    }
}
