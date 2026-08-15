#include "addressbook.h"
#include "log.h"

// Глобальные переменные (определения)
Addressbook::Contact myProfile;
std::map<uint16_t, Addressbook::Contact> contacts;

void Addressbook::setMyProfile(uint16_t id, const Contact& profile) {
    std::unique_lock lock(ab_mutex);
    my_id = id;
    myProfile = profile;
    //std::cout << "[Addressbook] Успешно сохранен собственный профиль. Мой ID: "
    //          << my_id << ", Публичный ключ: " << myProfile.key << std::endl;

    Log::info("Addressbook", "MAC-адрес интерфейса: ", my_id, ", pubkey: ", myProfile.key);
}

void Addressbook::setContact(uint16_t id, const Contact& contact) {
    std::unique_lock lock(ab_mutex);
    contacts[id] = contact;
    //std::cout << "[Addressbook] Обновлен контакт [" << id << "] (Канал: " << contact.chanComm << ", Скорость: " << contact.netsp << ")" << std::endl;
    Log::info("Addressbook", "Обновлен контакт MAC ", id, ", chanComm ", contact.chanComm, ", netsp ", contact.netsp);

}

void Addressbook::setContacts(const std::map<uint16_t, Contact>& new_contacts) {
    std::unique_lock lock(ab_mutex);
    for (const auto& [id, contact] : new_contacts) {
        contacts[id] = contact;
    }
    //std::cout << "[Addressbook] Пакетно загружено контактов из системы: " << new_contacts.size() << std::endl;
    Log::info("Addressbook", "Из интерфейса загружено ", new_contacts.size(), " контактов");
}

void Addressbook::clear() {
    std::unique_lock lock(ab_mutex);
    contacts.clear();
    my_id = 0;
    //std::cout << "[Addressbook] Адресная книга полностью очищена." << std::endl;
    Log::info("Addressbook", "Адресная книга очищена");
}

uint16_t Addressbook::getMyId() const {
    std::shared_lock lock(ab_mutex);
    return my_id;
}

bool Addressbook::getContact(uint16_t id, Contact& out_contact) const {
    std::shared_lock lock(ab_mutex);
    auto it = contacts.find(id);
    if (it != contacts.end()) {
        out_contact = it->second;
        return true;
    }
    return false;
}

void Addressbook::print() const {
    std::shared_lock lock(ab_mutex);
    std::cout << "\n=========================================================" << std::endl;
    std::cout << "             АКТУАЛЬНАЯ АДРЕСНАЯ КНИГА В ПАМЯТИ           " << std::endl;
    std::cout << "---------------------------------------------------------" << std::endl;
    std::cout << "  * Мой собственный Сетевой ID: ";
    if (my_id == 0) std::cout << "НЕ ОПРЕДЕЛЕН" << std::endl;
    else std::cout << my_id << std::endl;

    if (!myProfile.key.empty()) std::cout << "  * Мой Публичный Ключ:          " << myProfile.key << std::endl;
    std::cout << "  * Всего обнаружено контактов:  " << contacts.size() << std::endl;

    for (const auto& [id, c] : contacts) {
        std::cout << "    - Узел [" << id << "] -> Канал (chanComm): " << static_cast<int>(c.chanComm)
                  << ", Скорость (netsp): " << static_cast<int>(c.netsp);

        if (!c.name.empty()) std::cout << ", Имя/Лейбл: \"" << c.name << "\"";
        std::cout << std::endl;
    }
    std::cout << "=========================================================\n" << std::endl;
}
