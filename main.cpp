#include "wsclient.h"
#include "msgparser.h"
#include "addressbook.h"
#include "console.h"
#include "tun.h"
#include "log.h"
#include "config.h"

#include <iostream>
#include <thread>
#include <chrono>

WSclient wsclient;
Console console;
TunInterface tun;
//MsgParser parser;
Config config;

//std::string g_serverAddr = "unknown";
//std::string g_tunName = "tun0"; // Имя TUN-интерфейса по умолчанию

void timerThread(WSclient* client) {
    while (client->running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        client->processTimers();
    }
}

int main(int argc, char **argv) {
    std::srand(std::time(nullptr));

    // 1. Загружаем настройки из файла (если существует)
    config.loadFromFile("pclient.conf");

    // 2. Парсим командную строку (имеет приоритет над конфигом)
    if (!config.parseCommandLine(argc, argv)) {
        return 1; // Ошибка парсинга или -help
    }

    //g_serverAddr = (argc >= 2) ? argv[1] : "puheg.local";



    // 1. Инициализация
    //wsclient.initContext();
    //wsclient.connectWS(g_serverAddr);
    //wsclient.connectWS(config.ws_address);


    // РЕГИСТРАЦИЯ КОЛЛБЕКА (без аргументов)
    Addressbook::setOnProfileReadyCallback([&]() {
        Log::info("Main", "Сигнал: Профиль устройства получен. Начинаем настройку TUN...");

        //Addressbook::Contact myProfile = Addressbook::getMyProfile();
        //uint32_t myIp = myProfile.ipAddr();

        Log::info("Main", "Мой MAC: ", myProfile.id, ", Вычисленный IP: ", myProfile.ipString());

        // TUN инициализируем только один раз при первом подключении
        if (!tun.isInitialized()) {
            if (tun.init(myProfile.ipAddr(), config.tun_interface, config.tun_netmask, config.tun_mtu)) {
                tun.start(&wsclient);
            } else {
                Log::info("Main", "TUN не инициализирован. Сетевой мост работать не будет.");
            }
        }
    });



    // 2. Потоки
    std::thread input_thread(&Console::consoleInputThread, &console, &wsclient);
    std::thread timers_thread(timerThread, &wsclient);
    //std::thread tun_thread(&TunInterface::tunReaderThread, &tun, &wsclient);

    std::cout << "[System] TCP/IP Клиент запущен. Вход в сетевой цикл..." << std::endl;

    // 3. Главный цикл (теперь работает корректно!)
    //while (wsclient.running) {
    //    wsclient.service(50);
    //}

    // === ГЛАВНЫЙ ЦИКЛ С РЕКОННЕКТОМ ===
    while (wsclient.running) {
        // 1. Инициализация (как в оригинале)
        wsclient.initContext();

        // 2. Подключение (как в оригинале)
        if (!wsclient.connectWS(config.ws_address)) {
            Log::info("Wsclient", "Не удалось создать контекст, повтор через 2 с...");
            wsclient.destroyContext();
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        // 3. Ожидание установления соединения (до 15 секунд)
        int waitAttempts = 0;
        while (wsclient.running && !wsclient.sessionAlive && waitAttempts < 300) {
        //while (wsclient.running && !wsclient.sessionAlive) {
            wsclient.service(50);
            waitAttempts++;
        }

        if (!wsclient.running) break;

        if (!wsclient.sessionAlive) {
            Log::info("Wsclient", "Таймаут подключения (5 с), реконнект...");
            wsclient.destroyContext();
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        Log::info("Main", "Соединение установлено, работаем.");

        // 4. Рабочий цикл (как в оригинале) — пока сессия жива
        while (wsclient.running && wsclient.sessionAlive) {
            wsclient.service(50);
        }

        // 5. Соединение потеряно — cleanup и пауза
        if (wsclient.running) {
            Log::info("Wsclient", "Соединение потеряно, реконнект через 1 с...");
            wsclient.destroyContext();
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    std::cout << "\n[System] Завершение работы..." << std::endl;

    if (input_thread.joinable()) input_thread.join();
    if (timers_thread.joinable()) timers_thread.join();

    tun.stop();
    tun.close();
    wsclient.destroyContext();

    std::cout << "[System] Работа программы успешно завершена." << std::endl;
    return 0;
}
