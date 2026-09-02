#ifndef CONFIG_H
#define CONFIG_H
#pragma once
#include <string>
#include <cstdint>

struct Config {
    uint16_t eventBusPort = 9400;
    std::string configPath = "pclient.conf"; // Путь по умолчанию

    bool loadFromFile(const std::string& filename);
    bool parseCommandLine(int argc, char** argv);
};

extern Config config;
#endif // CONFIG_H
