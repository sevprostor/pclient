#include "ebparser.h"
#include "config.h"
//#include "transport.h"
#include "log.h"      // <-- единый вывод
#include "../libs/json.hpp"
#include "words.h"
#include "file.h"
#include <string>

using json = nlohmann::json;

//File file;

void EBParser::parseEmsg(const EBMessage& msg, File& file) {
    if (!msg.evenbus) return;

    json j = json::parse(msg.rawtext, nullptr, false);
    if (j.is_discarded()) return;

    if (j.contains("netprofile")) netprofileTopic(msg, file);
    if (j.contains("words")) wordsTopic(msg, file);
    //if (j.contains("process")) processTopic(msg);
}

void EBParser::netprofileTopic(const EBMessage& msg, File& file) {
    json j = json::parse(msg.rawtext, nullptr, false);
    if (j.is_discarded()) {
        Log::error("EBParser", "❌ Ошибка парсинга JSON");
        return;
    }

    if (!j.contains("netprofile") || !j["netprofile"].contains("contacts")) {
        Log::warn("EBParser", "⚠️ В JSON отсутствует netprofile.contacts");
        return;
    }

    const auto& np = j["netprofile"];
    const auto& contacts = np["contacts"];

    if (!contacts.is_array()) {
        Log::error("EBParser", "❌ netprofile.contacts не является массивом");
        return;
    }

    Log::info("EBParser", "📡 NetProfile получен, контактов: ", contacts.size());

    // Очищаем адресную книгу перед заполнением
    config.addressbook.clear();

    Config::abc contact;
    for (const auto& c : contacts) {
        uint16_t id = static_cast<uint16_t>(c.value("id", 0));
        std::string ip = c.value("ip", "");
        std::string key = c.value("key", "");

        contact.mac = id;
        contact.ip = ip;
        contact.key = key;

        // Проверяем флаг myOwn
        if (c.contains("myOwn")) {
            config.myContact = contact;
            Log::info("EBParser", "  👤 Мой контакт: id=", id, ", ip=", ip);
        } else {
            config.addressbook.emplace_back(contact);
            Log::info("EBParser", "  - id=", id, ", ip=", ip);
        }
    }

    Log::info("EBParser", "✅ Адресная книга загружена: ", config.addressbook.size(), " контактов");
}

void EBParser::wordsTopic(const EBMessage& msg, File& file){

    Words words;

    //std::string topic = msg.rawtext;
    json j = json::parse(msg.rawtext, nullptr, false);

    std::vector<uint8_t> bytes = Words::hexToBytes(j["words"].value("payload", std::string("")));

    if (bytes.size() < 3 || bytes[0] != 'P' || bytes[1] != 'W' || bytes[2] != 0x01) {
        Log::warn("ListenWords", "⚠️ Отсутствует магик PW\\x01");
        return;
    }
    std::vector<uint8_t> body(bytes.begin() + 3, bytes.end());

    Envelope env;
    if (!words.parseEnvelope(body, env)) return;

    switch (env.type) {
    case 'F': {
        PwFile pwFile;
        if (words.parseFile(body, pwFile)) {
            Log::info("ListenWords", "📥 FILE: ", pwFile.name,
                      " part ", (int)pwFile.part, "/", (int)pwFile.totalParts,
                      " uuid=", pwFile.uuid,
                      " sender=", pwFile.env.sender,
                      " content=", pwFile.content.size(), " байт");

            // Сборка файла
            if (file.assembleFile(pwFile)) {
                Log::info("ListenWords", "✅ Файл полностью получен!");
            }
        }
        break;
    }
    case '0': {
        PwProof proof;
        if (words.parseProof(body, proof)) {
            Log::info("ListenWords", "🔑 PROOF от sender=", proof.env.sender);
        }
        break;
    }
    case 'C': {
        PwCommand cmd;
        if (words.parseCommand(body, cmd)) {
            Log::info("ListenWords", "⚡ COMMAND от sender=", cmd.env.sender);
        }
        break;
    }
    default:
        Log::warn("ListenWords", "⚠️ Неизвестный тип пакета: '", env.type, "'");
    }


}
