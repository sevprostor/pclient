#include "config.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>

bool Config::loadFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return false; // Файл не найден - это нормально, используем defaults
    }

    std::string line;
    while (std::getline(file, line)) {
        // Пропускаем комментарии и пустые строки
        if (line.empty() || line[0] == '#') continue;

        // Ищем '='
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;

        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);

        // Убираем пробелы
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);

        // Применяем настройки
        if (key == "ws_address") ws_address = value;
        else if (key == "tun_interface") tun_interface = value;
        else if (key == "tun_ip") tun_ip = value;
        else if (key == "tun_netmask") tun_netmask = std::stoi(value);
        else if (key == "tun_mtu") tun_mtu = std::stoi(value);
    }

    file.close();
    return true;
}

bool Config::parseCommandLine(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        // Проверяем, что это ключ (начинается с '-')
        if (arg[0] != '-') {
            std::cerr << "Неизвестный аргумент: " << arg << std::endl;
            return false;
        }

        if (arg == "-ws") {
            if (i + 1 < argc) {
                ws_address = argv[i + 1];
                i++;
            } else {
                std::cerr << "Ошибка: -ws требует адрес" << std::endl;
                return false;
            }
        }
        else if (arg == "-tun") {
            if (i + 1 < argc) {
                tun_interface = argv[i + 1];
                i++;
            } else {
                std::cerr << "Ошибка: -tun требует имя интерфейса" << std::endl;
                return false;
            }
        }
        //else if (arg == "-ip") {
        //    if (i + 1 < argc) {
        //        tun_ip = argv[i + 1];
        //        i++;
        //    } else {
        //        std::cerr << "Ошибка: -ip требует IP-адрес" << std::endl;
        //        return false;
        //    }
        //}
        else if (arg == "-mask") {
            if (i + 1 < argc) {
                tun_netmask = std::stoi(argv[i + 1]);
                i++;
            } else {
                std::cerr << "Ошибка: -mask требует число" << std::endl;
                return false;
            }
        }
        else if (arg == "-mtu") {
            if (i + 1 < argc) {
                tun_mtu = std::stoi(argv[i + 1]);
                i++;
            } else {
                std::cerr << "Ошибка: -mtu требует число" << std::endl;
                return false;
            }
        }
        else if (arg == "-config") {
            if (i + 1 < argc) {
                loadFromFile(argv[i + 1]);
                i++;
            } else {
                std::cerr << "Ошибка: -config требует имя файла" << std::endl;
                return false;
            }
        }
        else if (arg == "-help" || arg == "-h") {
            std::cout << "Использование:\n"
                      << "  -ws <address>    Адрес WebSocket сервера (по умолчанию: puheg.local)\n"
                      << "  -tun <name>      Имя TUN интерфейса (по умолчанию: tun0)\n"
                      //<< "  -ip <address>    IP-адрес TUN интерфейса (по умолчанию: 10.0.0.1)\n"
                      << "  -mask <number>   Маска сети (по умолчанию: 24)\n"
                      << "  -mtu <number>    MTU интерфейса (по умолчанию: 1400)\n"
                      << "  -config <file>   Загрузить настройки из файла\n"
                      << "  -help            Показать эту справку\n";
            return false;
        }
        else {
            std::cerr << "Неизвестный ключ: " << arg << std::endl;
            return false;
        }
    }
    return true;
}

void Config::print() const {
    std::cout << "\n=== Текущие настройки ===\n"
              << "WebSocket: " << ws_address << "\n"
              << "TUN интерфейс: " << tun_interface << "\n"
              << "TUN IP: " << tun_ip << "/" << tun_netmask << "\n"
              << "TUN MTU: " << tun_mtu << "\n"
              << "=========================\n" << std::endl;
}
