#ifndef CONFIG_H
#define CONFIG_H
#pragma once
#include <string>
#include <cstdint>
#include <vector>

struct Config {
    uint16_t eventBusPort = 9400;
    std::string workDir = "./";
    std::string configPath = "pclient.conf"; // Путь по умолчанию

    struct abc{

        uint16_t mac;
        std::string ip;
        std::string key;

    }cnt;

    abc myContact;
    std::vector<abc> addressbook;


    //bool loadFromFile(const std::string& filename);
    //bool parseCommandLine(int argc, char** argv);
};

extern Config config;
#endif // CONFIG_H
