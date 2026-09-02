#ifndef HAULER_H
#define HAULER_H
#pragma once
#include "transport.h"
#include <string>
#include <atomic>

class Hauler {
public:
    Hauler(Transport& transport);

    bool sendFile(const std::string& filePath, const std::string& destIp);
    void recvFiles(const std::string& outDir);
    void stop();

private:
    Transport& transport_;
    std::atomic<bool> running_{false};
};
#endif // HAULER_H
