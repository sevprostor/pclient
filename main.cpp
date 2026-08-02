#include "wsclient.h"
#include "msgparser.h"
#include "addressbook.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <mutex>

#include <termios.h>   // Для управления терминалом в Linux
#include <unistd.h>    // Для read()

WSclient wsclient;
MsgParser parser;




static struct lws_protocols protocols[] = {
    { "qos2-protocol", WSclient::callbackQos2Client, 0, 0, 0, nullptr, 0 },
    { nullptr, nullptr, 0, 0, 0, nullptr, 0 }
};


// Глобальные переменные для "умной" консоли
std::mutex consoleMutex;
std::string g_currentInput; // Хранит то, что пользователь печатает прямо сейчас
std::string g_serverAddr = "unknown"; // <-- ДОБАВЛЕНО: для надежного хранения адреса


// Функция для получения актуального системного промпта
std::string getSystemPrompt() {
    // 1. Получаем адрес подключения (если пустой, ставим заглушку)
    //std::string addr = wsclient.serverAddress.empty() ? "unknown" : wsclient.serverAddress;

    // 2. Получаем ваш MyId.
    // ЗАМЕНИТЕ эту строку на реальный вызов, например: Addressbook::getMyId()
    // или использование вашей глобальной переменной, где хранится ID.
    int myId = 0; // <-- ЗАМЕНИТЕ НА РЕАЛЬНОЕ ЗНАЧЕНИЕ, например: Addressbook::myId

    // Формируем строку. Можно добавить ANSI-цвета для красоты (зеленый цвет):
    // \033[1;32m - жирный зеленый, \033[0m - сброс цвета
    return "\033[1;32m" + g_serverAddr + "@" + std::to_string(myProfile.id) + "\033[0m > ";
}

// Функция для перерисовки приглашения и текущего ввода
void redrawPrompt() {
    // Используем printf вместо std::cout для надежности в raw-режиме
    printf("\r\033[2K%s%s", getSystemPrompt().c_str(), g_currentInput.c_str());
    fflush(stdout);
}

std::string readLineWithRedraw() {
    g_currentInput.clear();

    // Сохраняем старые настройки терминала
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;

    // Отключаем канонический режим (построчный ввод) и эхо (автоматический вывод символов)
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    redrawPrompt(); // Рисуем начальное "> "

    while (wsclient.running) {
        char c;
        // Читаем один символ
        if (read(STDIN_FILENO, &c, 1) < 0) break;

        if (c == '\n' || c == '\r') {
            std::cout << std::endl; // Финальный перенос строки при нажатии Enter
            break;
        }
        else if (c == 127 || c == 8) { // Backspace (ASCII 127 или 8)
            if (!g_currentInput.empty()) {
                g_currentInput.pop_back();
                redrawPrompt();
            }
        }
        else if (c >= 32 && c <= 126) { // Печатаемые символы (буквы, цифры, пробел)
            g_currentInput += c;
            redrawPrompt();
        }
        // Примечание: стрелки влево/вправо здесь не обрабатываются для простоты,
        // но базовый ввод и стирание работают идеально.
    }

    // Восстанавливаем нормальные настройки терминала
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);

    std::string result = g_currentInput;
    g_currentInput.clear();
    return result;
}

// Поток интерактивного ввода
void consoleInputThread(WSclient* client) {
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    {
        std::lock_guard<std::mutex> lock(consoleMutex);
        std::cout << "\nФормат: what.todo.howmuch. msg" << std::endl;
        std::cout << "Пример: toss..1234. hello" << std::endl;
        std::cout << "Для выхода введите 'exit'\n" << std::endl;
        //std::cout << "> " << std::flush;
    }

    while (client->running) {
        /*std::string line;

        {
            std::lock_guard<std::mutex> lock(consoleMutex);
            std::getline(std::cin, line);
        }*/

        // Используем нашу новую функцию вместо std::getline
        std::string line = readLineWithRedraw();

        if (line == "exit" || line == "quit") {
            client->running = 0;
            break;
        }

        if (!line.empty()) {
            MsgParser::PuhegUpperMessage pumsg; // Чистый экземпляр для каждой команды
            parser.parseFlatCommand(line, &pumsg);
            client->sendMessage(&pumsg);

            /*
            std::lock_guard<std::mutex> lock(consoleMutex);
            std::cout << "> " << std::flush;
            */
            // После отправки команды курсор и так будет внизу,
            // но на всякий случай перерисуем промпт для следующей команды
            std::lock_guard<std::mutex> lock(consoleMutex);
            redrawPrompt();
        }
    }
}

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
    std::thread input_thread(consoleInputThread, &wsclient);
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
