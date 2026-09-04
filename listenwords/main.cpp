#include "config.h"
#include "transport.h"
#include "log.h"      // <-- единый вывод
#include "../libs/json.hpp"
#include "words.h"
#include "file.h"
#include <string>

using json = nlohmann::json;

static void usage(const char* prog) {
    Log::info("ListenWords", "listenwords — приём событий шины и PW-кадров");
    Log::info("ListenWords", "  ", prog, " [-eb port] [-config path]");
}

static std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        bytes.push_back(static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

int main(int argc, char** argv) {
    // Предварительный проход: ищем -config
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "-config" && i + 1 < argc)
            config.configPath = argv[i+1];
    }

    // Загружаем конфиг и применяем аргументы командной строки
    config.loadFromFile(config.configPath);
    if (!config.parseCommandLine(argc, argv)) {
        usage(argv[0]);
        return 1;
    }

    Transport transport;
    if (!transport.init(config.eventBusPort)) {
        Log::error("ListenWords", "Не удалось инициализировать Transport на порту ",
                   config.eventBusPort);
        return 1;
    }

    Log::info("ListenWords", "Слушаю EventBus на порту ", config.eventBusPort, "...");
    /////////////////////////////////////////////

        // ... конфиг и transport.init(...) без изменений ...

        Words words;
        File file;         // <-- НОВОЕ: объект для сборки файлов

        transport.setOnEvent([&words, &file](const std::string& text) {
            json j = json::parse(text, nullptr, false);
            if (j.is_discarded()) {
                Log::warn("ListenWords", "Не-JSON данные: ", text);
                return;
            }

            if (!j.contains("words")) {
                Log::info("ListenWords", "📨 ", j.dump());
                return;
            }

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
        });

        while (true) transport.poll(100);
        return 0;
    }
