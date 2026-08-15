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
    wsclient.initContext();
    //wsclient.connectWS(g_serverAddr);
    wsclient.connectWS(config.ws_address);


    // 2. Открываем TUN-интерфейс
    bool tunEnabled = false;
    //if (tun.open(g_tunName)) {
    if (tun.open(config.tun_interface)) {
        tunEnabled = true;
        Log::info("Main", "TUN-интерфейс '", config.tun_interface, "' успешно открыт.");
    } else {
        Log::error("Main", "Не удалось открыть TUN. Работа без сетевого моста.");
    }

    // 2. Потоки
    std::thread input_thread(&Console::consoleInputThread, &console, &wsclient);
    std::thread timers_thread(timerThread, &wsclient);
    std::thread tun_thread(&TunInterface::tunReaderThread, &tun, &wsclient);

    std::cout << "[System] TCP/IP Клиент запущен. Вход в сетевой цикл..." << std::endl;

    // 3. Главный цикл (теперь работает корректно!)
    while (wsclient.running) {
        wsclient.service(50);
    }

    // 4. Завершение
    if (input_thread.joinable()) input_thread.join();
    if (timers_thread.joinable()) timers_thread.join();
    if (tun_thread.joinable()) tun_thread.join();

    wsclient.destroyContext(); // Теперь работает корректно!

    std::cout << "[System] Работа программы успешно завершена." << std::endl;
    return 0;
}
