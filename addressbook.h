#pragma once

#ifndef ADDRESSBOOK_H
#define ADDRESSBOOK_H

#include <string>
#include <map>
#include <iostream>
#include <shared_mutex>
#include <cstdint>
#include <mutex>

class Addressbook {
private:
    Addressbook() = default;

    uint16_t my_id = 0;
    mutable std::shared_mutex ab_mutex;

public:
    struct Contact {
        uint16_t id = 0;
        uint8_t chanComm = 0;
        uint8_t netsp = 0;
        std::string key = "";
        std::string name = "";
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

    uint16_t getMyId() const;
    bool getContact(uint16_t id, Contact& out_contact) const;
};

// Глобальные переменные (объявления)
extern Addressbook::Contact myProfile;
extern std::map<uint16_t, Addressbook::Contact> contacts; // <-- ИСПРАВЛЕНО

#endif // ADDRESSBOOK_H
