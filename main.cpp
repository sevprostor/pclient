#include "wsclient.h"
#include "msgparser.h"
#include "addressbook.h"
#include "console.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <mutex>

#include <termios.h>   // Для управления терминалом в Linux
#include <unistd.h>    // Для read()

WSclient wsclient;
Console console;
//MsgParser parser;




static struct lws_protocols protocols[] = {
    { "qos2-protocol", WSclient::callbackQos2Client, 0, 0, 0, nullptr, 0 },
    { nullptr, nullptr, 0, 0, 0, nullptr, 0 }
};

std::string g_serverAddr = "unknown"; // <-- ДОБАВЛЕНО: для надежного хранения адреса


// НОВЫЙ ПОТОК: Независимый обработчик таймеров
void timerThread(WSclient* client) {
    while (client->running) {
        // Спим ровно 500 мс. Это гарантирует, что таймеры будут проверяться
        // с точностью до миллисекунды, независимо от блокировок lws_service.
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        client->processTimers();
    }
}

int main(int argc, char **argv) {
    std::srand(std::time(nullptr));
    wsclient.initContext();

    struct lws_context_creation_info info;
    std::memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.user = &wsclient;

    struct lws_context *lws_ctx = lws_create_context(&info);
    if (!lws_ctx) {
        std::cerr << "Critical: lws_create_context failed" << std::endl;
        return 1;
    }

    // Сохраняем адрес в глобальную переменную ДЛЯ ПРОМПТА
    g_serverAddr = (argc >= 2) ? argv[1] : "puheg.local";

    struct lws_client_connect_info i;
    std::memset(&i, 0, sizeof(i));
    i.context = lws_ctx;
    i.address = (argc >= 2) ? argv[1] : "puheg.local";
    i.port = 80;
    i.path = "/ws";
    i.host = i.address;
    i.origin = i.address;
    i.protocol = protocols[0].name;

    lws_client_connect_via_info(&i);
    std::cout << "[System] TCP/IP Клиент запущен. Вход в сетевой цикл..." << std::endl;

    // Запускаем потоки
    std::thread input_thread(&Console::consoleInputThread, &console, &wsclient);
    std::thread timers_thread(timerThread, &wsclient); // <-- Запуск независимого потока таймеров

    // ГЛАВНЫЙ ЦИКЛ: Теперь он отвечает ТОЛЬКО за сетевые события libwebsockets
    while (wsclient.running) {
        // Можно оставить 50 или 0. Поскольку таймеры вынесены в отдельный поток,
        // блокировка lws_service больше не влияет на логику приложения.
        lws_service(lws_ctx, 50);
    }

    // Корректное завершение
    if (input_thread.joinable()) {
        std::cout << "[System] Нажмите Enter для завершения работы..." << std::endl;
        input_thread.join();
    }

    // Останавливаем поток таймеров (он выйдет из цикла, так как running = 0)
    if (timers_thread.joinable()) {
        timers_thread.join();
    }

    lws_context_destroy(lws_ctx);
    std::cout << "[System] Работа программы успешно завершена." << std::endl;
    return 0;
}
