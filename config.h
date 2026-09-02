#ifndef CONFIG_H
#define CONFIG_H
#pragma once
#include <string>
#include <cstdint>

struct Config {

    // Режим ввода в командной строке драйвера
    std::string cmdMode = "MAC";

    // WebSocket
    std::string ws_address = "puheg.local";

    // TUN интерфейс
    std::string tun_interface = "tun0";
    std::string tun_ip = "10.0.0.1";
    int tun_netmask = 24;
    int tun_mtu = 1400;

    //EventBus
    int eventBusPort = 9400;

    // Загрузка из файла
    bool loadFromFile(const std::string& filename);

    // Парсинг командной строки
    bool parseCommandLine(int argc, char** argv);

    // Вывод текущих настроек
    void print() const;
};

extern Config config;

#endif // CONFIG_H
