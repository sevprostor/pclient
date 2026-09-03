#ifndef WORDS_H
#define WORDS_H
#pragma once
#include "transport.h"
#include <string>
#include <atomic>

class PuhegWords {
public:
    PuhegWords(Transport& transport);

    bool sendFile(const std::string& filePath, const std::string& destIp);
    void recvFiles(const std::string& outDir);
    void stop();

private:
    Transport& transport_;
    std::atomic<bool> running_{false};
};
#endif // HAULER_H
