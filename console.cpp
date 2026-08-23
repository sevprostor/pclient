#include "console.h"
#include "addressbook.h"
#include <iostream>
#include <termios.h>
#include <unistd.h>
#include "config.h"

// Объявляем внешние переменные
//extern std::string g_serverAddr;
extern Config config;

extern Addressbook::Contact myProfile; // Убедитесь, что это объявлено в addressbook.h!

MsgParser parser;
extern TCPclient wsclient;

// Глобальные переменные для "умной" консоли
std::mutex consoleMutex;
std::string g_currentInput; // Хранит то, что пользователь печатает прямо сейчас
//std::string g_serverAddr = "unknown"; // <-- ДОБАВЛЕНО: для надежного хранения адреса

// Функция для перерисовки приглашения и текущего ввода
void Console::redrawPrompt() {

    const std::string sysPrompt = "\r\033[2K\033[1;32m" +
                                  config.ws_address + "@" + std::to_string(myProfile.id) +
                                  + " (" + config.cmdMode + ") "
                                  ">\033[0m ";

    // Используем printf вместо std::cout для надежности в raw-режиме
    //printf("%s%s", getSystemPrompt().c_str(), g_currentInput.c_str());
    printf("%s%s", sysPrompt.c_str(), g_currentInput.c_str());
    fflush(stdout);
}

std::string Console::readLineWithRedraw() {
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
void Console::consoleInputThread(TCPclient* client) {
    std::this_thread::sleep_for(std::chrono::milliseconds(150));



    while (client->running) {
        /*std::string line;

        {
            std::lock_guard<std::mutex> lock(consoleMutex);
            std::getline(std::cin, line);
        }*/

        // Используем нашу новую функцию вместо std::getline
        std::string line = readLineWithRedraw();

        //Здесь делать парсер команд
        if (line == "exit" || line == "quit") {
            client->running = 0;
            break;
        } else if (line == "drv"){

            //переключиться в режим приема команд драйвера
            config.cmdMode = "DRV";
            line = "";

        } else if (line == "mac"){

            //Переключиться на прием команд мак-уровня
            config.cmdMode = "MAC";
            line = "";

        } else if (line == "help"){

            {
                std::lock_guard<std::mutex> lock(consoleMutex);
                std::cout << "\n\"drv\" - driver command mode" << std::endl;

                std::cout << "\"l2\" - MAC command mode" << std::endl;

                std::cout << "\nФормат: what.todo.howmuch. msg" << std::endl;
                std::cout << "Пример: toss..1234. hello" << std::endl;
                std::cout << "Для выхода введите 'exit'\n" << std::endl;
                //std::cout << "> " << std::flush;
            }

            line = "";


        }

        if (config.cmdMode == "MAC" && !line.empty()) {
            MsgParser::PuhegUpperMessage pumsg; // Чистый экземпляр для каждой команды
            parser.parseFlatCommand(line, &pumsg);
            client->sendMessage(&pumsg);

            // После отправки команды курсор и так будет внизу,
            // но на всякий случай перерисуем промпт для следующей команды
            //std::lock_guard<std::mutex> lock(consoleMutex);
            redrawPrompt();
        }
    }
}
