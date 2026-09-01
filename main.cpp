//#include "wsclient.h"
#include "TCPclient.h"
#include "msgparser.h"
#include "addressbook.h"
#include "console.h"
#include "tun.h"
#include "log.h"
#include "config.h"
#include "mdns_discovery.h"
#include "eventbus.h"

#include <iostream>
#include <thread>
#include <chrono>

//WSclient wsclient;
TCPclient wsclient;
Console console;
TunInterface tun;
//MsgParser msgprs;
Config config;

//std::string g_serverAddr = "unknown";
//std::string g_tunName = "tun0"; // Имя TUN-интерфейса по умолчанию

void timerThread(TCPclient* client) {
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

    //Поиск других станций в локалке, если есть
    //Перенести в класс
    auto nodes = discoverPuhegNodes(3000);
    if (nodes.empty()) {
        Log::warn("MDNS", "В сети не найдено узлов puheg");
    } else {
        Log::info("MDNS", "===============================");
        Log::info("MDNS", "Puheg станции в локальной сети:");
        for (const auto& n : nodes) {
            Log::info("MDNS", "Имя: ", n.instance, " -> ", n.ip, ":", n.port);
        }
        Log::info("MDNS", "===============================");
    }

    //Шина событий/сообщений для внешних программ
    EventBus::init(config.event_port);


    // === ГЛАВНЫЙ ЦИКЛ С РЕКОННЕКТОМ ===

    static bool initialized = false;

    while (wsclient.running) {
        wsclient.initContext();

        if (!wsclient.connectTCP(config.ws_address)) {

            Log::warn("TCPclient", "Не удалось подключиться, повтор через 5 с...");
            wsclient.destroyContext();
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;

        } else if(!initialized){

            //При первом подключении синхронизировать время на станции и инициализировать клиент
            wsclient.sendTimeSync();
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            MsgParser mParser;
            MsgParser::PuhegUpperMessage pumsg;
            pumsg.what = "initclient";
            mParser.packMessage(&pumsg);
            wsclient.sendMessage(&pumsg);



            initialized = true;
        }

        int waitAttempts = 0;
        while (wsclient.running && !wsclient.sessionAlive && waitAttempts < 300) {
            wsclient.service(50);
            waitAttempts++;
        }

        if (!wsclient.sessionAlive) {
            Log::warn("TCPclient", "Таймаут подключения");
            wsclient.destroyContext();
            continue;
        }

        while (wsclient.running && wsclient.sessionAlive) {
            wsclient.service(50);
        }

        if (wsclient.running) {
            Log::warn("TCPclient", "Соединение потеряно, реконнект через 5 с...");
            wsclient.destroyContext();
            std::this_thread::sleep_for(std::chrono::seconds(5));
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
