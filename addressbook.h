#pragma once

#ifndef ADDRESSBOOK_H
#define ADDRESSBOOK_H

#include <string>
#include <map>
#include <iostream>
#include <shared_mutex>
#include <cstdint>
#include <mutex>
#include <functional>
#include <arpa/inet.h>

class Addressbook {
private:
    Addressbook() = default;



    uint16_t my_id = 0;
    mutable std::shared_mutex ab_mutex;

    static std::function<void()> onProfileReadyCallback;

public:

    std::shared_mutex& getMutex() { return ab_mutex; }
    /*
    struct Contact {
        uint16_t id = 0;
        uint8_t chanComm = 0;
        uint8_t netsp = 0;
        std::string key = "";
        std::string name = "";

        // Хелперы для удобной работы с IP


        // Вычисление MAC из IP (ваша формула)
        //uint16_t idFromIp() const {
        //    uint8_t octet3 = (ip >> 8) & 0xFF;
        //    uint8_t octet4 = ip & 0xFF;
        //    return (octet3 << 8) | octet4;
        //}

        // Вычисление IP из MAC (обратная формула)
        uint32_t ipAddr() const {
            uint8_t octet3 = id / 256;
            uint8_t octet4 = id % 256;
            // Формируем IP: 10.0.octet3.octet4 в сетевом порядке байт
            return (10 << 24) | (0 << 16) | (octet3 << 8) | octet4;
        }

        std::string ipString() const {
            if (id == 0) return "0.0.0.0";
            uint8_t octet3 = id / 256;
            uint8_t octet4 = id % 256;
            return "10.0." + std::to_string(octet3) + "." + std::to_string(octet4);
        }
    };*/

    struct Contact {
        uint16_t id = 0;
        uint8_t chanComm = 0;
        uint8_t netsp = 0;
        std::string key = "";
        std::string name = "";

        // Вычисление IP из MAC (возвращает значение в СЕТЕВОМ порядке байт)
        uint32_t ipAddr() const {
            uint8_t octet3 = id / 256;
            uint8_t octet4 = id % 256;
            // Формируем IP: 10.0.octet3.octet4 и конвертируем в сетевой порядок байт
            return htonl((10u << 24) | (0u << 16) | (octet3 << 8) | octet4);
        }

        // Преобразование IP в строку (использует ipAddr и inet_ntop)
        std::string ipString() const {
            if (id == 0) return "0.0.0.0";

            uint32_t ip = ipAddr();
            char buf[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &ip, buf, INET_ADDRSTRLEN);
            return std::string(buf);
        }
    };

    static Addressbook& getInstance() {
        static Addressbook instance;
        return instance;
    }

    void setMyProfile(uint16_t id, const Contact& profile);
    void setContact(uint16_t id, const Contact& contact);
    void setContacts(const std::map<uint16_t, Contact>& new_contacts);
    void clear();
    void print() const;

    // Регистрация коллбека, который сработает при получении нашего профиля
    static void setOnProfileReadyCallback(std::function<void()> callback);

    uint16_t getMyId() const;
    bool getContact(uint16_t id, Contact& out_contact) const;
};

// Глобальные переменные (объявления)
extern Addressbook::Contact myProfile;
extern std::map<uint16_t, Addressbook::Contact> contacts; // <-- ИСПРАВЛЕНО

#endif // ADDRESSBOOK_H
