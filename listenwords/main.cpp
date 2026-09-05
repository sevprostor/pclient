#include "config.h"
#include "transport.h"
#include "log.h"      // <-- единый вывод
#include "../libs/json.hpp"
#include "words.h"
#include "file.h"
#include "ebparser.h"
#include "ebparser.h"
#include <string>
#include <thread>
#include <fstream>
//File file;
Config config;

//Words words;
//File file;


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

        //Words words;
    File file;         // <-- НОВОЕ: объект для сборки файлов


    while (true) {
        EBMessage emsg = transport.poll(100);
        if (emsg.evenbus) EBParser::parseEmsg(emsg, file);

        // Если адресная книга загружена — сканируем outbox и отправляем файлы
        if (!config.addressbook.empty()) {

            // 1. По 1 самому старому файлу из каждого каталога контакта
            auto candidates = file.scanOutboxAll();

            if (!candidates.empty()) {
                // 2. Выбираем самый старый среди кандидатов
                auto oldest = std::min_element(candidates.begin(), candidates.end(),
                                               [](const File::OutboxFile& a, const File::OutboxFile& b) {
                                                   return a.timeCreated < b.timeCreated;
                                               });

                // 3. Нарезаем в spool/<destIp>-<uuid>/ и удаляем исходник.
                //    Отправки пока нет — только спуллинг.
                file.spoolFile(*oldest, 2000);
            }

        }

        // Задержка между итерациями главного цикла
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }




        return 0;
    }
