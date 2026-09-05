#include "ebparser.h"
#include "config.h"
//#include "transport.h"
#include "log.h"      // <-- единый вывод
#include "../libs/json.hpp"
#include "words.h"
#include "file.h"
#include <string>

using json = nlohmann::json;



void EBParser::parseEmsg(std::string* text) {

    //Разбор шинного сообщения

    json eventExMsg = json::parse(text->data(), nullptr, false);
    if (eventExMsg.is_discarded()) {
        Log::warn("ListenWords", "Не-JSON данные: ", text);
        return;
    }

    if(eventExMsg.contains("netprofile")) netprofileTopic(text);

    if(eventExMsg.contains("words")) wordsTopic(text);


}

void EBParser::netprofileTopic(std::string* text){
    Log::info("EBParser", "Netprofile rcvd: ", text->data());

}

void EBParser::wordsTopic(std::string* topic){

    Words words;
    File file;

    json j = json::parse(topic->data(), nullptr, false);

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
