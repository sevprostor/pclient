#ifndef MDNS_DISCOVERY_H
#define MDNS_DISCOVERY_H

#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct PuhegNode {
    std::string instance;   // puheg, puheg-1620, ...
    std::string ip;
    uint16_t port = 0;
};

// Шлёт PTR-запрос _puheg._tcp.local и собирает ответы в течение timeout_ms
std::vector<PuhegNode> discoverPuhegNodes(int timeout_ms = 3000);

#endif // MDS_DISCOVERY_H
