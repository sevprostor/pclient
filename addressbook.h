#pragma once

#ifndef ADDRESSBOOK_H
#define ADDRESSBOOK_H

#include <string>
#include <map>
#include <iostream>
#include <shared_mutex>
#include <cstdint> // Для гарантированных типов uint16_t, int32_t
#include <mutex>

// Структура контакта, оптимизированная по памяти под TCP/IP стек
struct Contact {
    uint16_t id = 0;    // ИЗМЕНЕНО: теперь это число uint16_t

    uint8_t chanComm = 0;
    uint8_t netsp = 0;

    //int32_t chanComm = -1;
    //int32_t netsp = -1;
    std::string key = "";
    std::string name = ""; //нужно ли это здесь?
};


class Addressbook {
private:
    Addressbook() = default;

    uint16_t my_id = 0; // ИЗМЕНЕНО: теперь это uint16_t (0 означает "не определен")
    Contact my_profile;

    // ИЗМЕНЕНО: Ключ карты теперь uint16_t для мгновенного поиска по хэшу/числу
    std::map<uint16_t, Contact> contacts;
    mutable std::shared_mutex ab_mutex;

public:
    static Addressbook& getInstance() {
        static Addressbook instance;
        return instance;
    }

    // Сохранение собственного профиля устройства в сети
    void setMyProfile(uint16_t id, const Contact& profile);

    // Добавление или обновление одного чужого контакта
    void setContact(uint16_t id, const Contact& contact);

    // Пакетное сохранение словаря контактов
    void setContacts(const std::map<uint16_t, Contact>& new_contacts);

    void clear();
    void print() const;

    // Геттеры для будущей маршрутизации и TCP/IP интеграции
    uint16_t getMyId() const;
    bool getContact(uint16_t id, Contact& out_contact) const;
};

#endif // ADDRESSBOOK_H
