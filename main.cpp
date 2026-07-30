#include "wsclient.h"
#include "msgparser.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <cstdlib>

WSclient wsclient;
MsgParser parser;

static struct lws_protocols protocols[] = {
    { "qos2-protocol", WSclient::callbackQos2Client, 0, 0, 0, nullptr, 0 },
    { nullptr, nullptr, 0, 0, 0, nullptr, 0 }
};

// Исправленный поток интерактивного ввода
void consoleInputThread(WSclient* client) {
    std::string line;
    MsgParser::PuhegUpperMessage pumsg;

    // Даем сетевому слою 100 мс на вывод стартовых логов, чтобы интерфейс консоли не перемешивался
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "\n[Console] Интерактивный режим готов. Формат: what.todo.howmuch. msg" << std::endl;
    std::cout << "[Console] Пример: toss..1234. hello" << std::endl;
    std::cout << "[Console] Для выхода введите 'exit'\n" << std::endl;
    std::cout << "> " << std::flush; // Рисуем каретку ввода и принудительно сбрасываем буфер

    while (client->running) {
        if (std::getline(std::cin, line)) {
            if (line == "exit" || line == "quit") {
                client->running = 0;
                break;
            }
            if (!line.empty()) {
                //std::vector<uint8_t> packed_bytes = MsgParser::parseFlatCommand(line);
                parser.parseFlatCommand(line, &pumsg);
                client->sendMessage(&pumsg);
            }
            std::cout << "> " << std::flush; // Возвращаем каретку после отправки команды
        }
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

    // Запускаем поток интерактивного ввода
    std::thread input_thread(consoleInputThread, &wsclient);

    // ИСПРАВЛЕННЫЙ ГЛАВНЫЙ СЕТЕВОЙ ЦИКЛ
    while (wsclient.running) {
        lws_service(lws_ctx, 0);
        wsclient.processTimers();

        // Увеличиваем задержку до 10 мс.
        // Это дает ОС достаточно времени, чтобы переключить контекст процессора на поток ввода std::getline
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (input_thread.joinable()) {
        // Если поток ввода завис на std::getline, выводим подсказку для закрытия терминала
        std::cout << "[System] Нажмите Enter для завершения работы..." << std::endl;
        input_thread.join();
    }

    lws_context_destroy(lws_ctx);
    std::cout << "[System] Работа программы успешно завершена." << std::endl;
    return 0;
}
