#include "config.h"
#include "transport.h"
#include "log.h"      // <-- единый вывод
#include "../libs/json.hpp"
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

    // Всё приходит как JSON (события и PW-кадры, завёрнутые в JSON)
    transport.setOnEvent([](const std::string& text) {
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded()) {
            Log::warn("ListenWords", "Не-JSON данные: ", text);
            return;
        }

        if (j.contains("words")) {
            const auto& w = j["words"];
            std::string hexPayload = w.value("payload", std::string(""));

            // Декодируем hex-строку в байты
            std::vector<uint8_t> bytes = hexToBytes(hexPayload);
            //for (size_t i = 0; i + 1 < hexPayload.size(); i += 2) {
            //    std::string byteStr = hexPayload.substr(i, 2);
            //    bytes.push_back(static_cast<uint8_t>(std::stoi(byteStr, nullptr, 16)));
            //}

            Log::info("ListenWords", "📦 PW кадр: size=", bytes.size(), " bytes");

            // Проверяем магик PW\x01 на декодированных байтах
            if (bytes.size() >= 6 && bytes[0] == 'P' && bytes[1] == 'W' && bytes[2] == 0x01) {
                Log::info("ListenWords", "✅ Магик PW\\x01 подтверждён");

                // Показываем декодированные байты в hex
                std::string hexDump;
                for (uint8_t b : bytes) {
                    char buf[4];
                    snprintf(buf, sizeof(buf), "%02x ", b);
                    hexDump += buf;
                }
                Log::info("ListenWords", "Hex: ", hexDump);

                // Пробуем вывести как текст (с пропуском магика)
                if (bytes.size() > 3) {
                    std::string textPart(bytes.begin() + 3, bytes.end());
                    Log::info("ListenWords", "Text: ", textPart);
                }
            } else {
                Log::warn("ListenWords", "⚠️ Магик PW\\x01 не найден в декодированных байтах");
            }
        } else {
            Log::info("ListenWords", "📨 ", j.dump());
        }
    });

    // Бесконечный цикл приёма
    while (true) transport.poll(100);

    return 0;
}
