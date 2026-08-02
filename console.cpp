#include "console.h"
#include "addressbook.h"
#include <iostream>
#include <termios.h>
#include <unistd.h>

// Объявляем внешние переменные
extern std::string g_serverAddr;
extern Addressbook::Contact myProfile; // Убедитесь, что это объявлено в addressbook.h!

std::string Console::getSystemPrompt() {
    std::string addr = g_serverAddr.empty() ? "unknown" : g_serverAddr;

    // Используем глобальную переменную myProfile
    uint16_t myId = myProfile.id;

    return "\033[1;32m" + addr + "@" + std::to_string(myId) + "\033[0m > ";
}

void Console::redrawPrompt() {
    // Блокируем только на момент перерисовки
    std::lock_guard<std::mutex> lock(s_mutex);
    printf("\r\033[2K%s%s", getSystemPrompt().c_str(), s_currentInput.c_str());
    fflush(stdout);
}

std::string Console::readLineWithRedraw() {
    // 1. Очищаем буфер ввода под защитой мьютекса
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_currentInput.clear();
    }

    // 2. Настраиваем терминал в raw-режим (БЕЗ логирования, чтобы не вызвать deadlock)
    struct termios oldt, newt;
    if (tcgetattr(STDIN_FILENO, &oldt) < 0) {
        return ""; // Тихо возвращаем пустую строку при ошибке
    }

    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO); // Отключаем канонический режим и эхо

    if (tcsetattr(STDIN_FILENO, TCSANOW, &newt) < 0) {
        return "";
    }

    // 3. Рисуем начальный промпт
    redrawPrompt();

    // 4. Цикл чтения символов
    while (true) {
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);

        if (n < 0 || n == 0) {
            break; // Ошибка или EOF
        }

        // Блокируем мьютекс только для изменения состояния и перерисовки
        std::lock_guard<std::mutex> lock(s_mutex);

        if (c == '\n' || c == '\r') {
            std::cout << std::endl; // Финальный перенос строки
            break;
        }
        else if (c == 127 || c == 8) { // Backspace
            if (!s_currentInput.empty()) {
                s_currentInput.pop_back();
                printf("\r\033[2K%s%s", getSystemPrompt().c_str(), s_currentInput.c_str());
                fflush(stdout);
            }
        }
        else if (c >= 32 && c <= 126) { // Печатаемые символы
            s_currentInput += c;
            printf("\r\033[2K%s%s", getSystemPrompt().c_str(), s_currentInput.c_str());
            fflush(stdout);
        }
    }

    // 5. Восстанавливаем настройки терминала
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);

    // 6. Возвращаем результат
    std::string result;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        result = s_currentInput;
        s_currentInput.clear();
    }

    return result;
}

std::string Console::getCurrentInput() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_currentInput;
}

std::mutex& Console::getMutex() {
    return s_mutex;
}
