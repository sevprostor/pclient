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
            // Пришло что-то не-JSON — показываем как есть
            Log::warn("ListenWords", "Не-JSON данные: ", text);
            return;
        }

        if (j.contains("words")) {
            // PW-кадр, завёрнутый в JSON драйвером
            const auto& w = j["words"];
            Log::info("ListenWords", "📦 PW кадр: size=", w.value("size", 0),
                      " payload=", w.value("payload", std::string("")));
        } else {
            // Обычное puheg-событие (process / transport / hardware / ...)
            Log::info("ListenWords", "📨 ", j.dump());
        }
    });

    // Бесконечный цикл приёма
    while (true) transport.poll(100);

    return 0;
}
