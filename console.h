#ifndef CONSOLE_H
#define CONSOLE_H

#pragma once
#include <string>
#include <mutex>

// Пространство имен или класс для изоляции логики терминала
class Console {
public:
    // Получить системный промпт вида "address@myId > "
    static std::string getSystemPrompt();

    // Перерисовать промпт и текущий ввод
    static void redrawPrompt();

    // Читать строку ввода с перерисовкой (блокирующая)
    static std::string readLineWithRedraw();

    // Геттеры для состояния (чтобы Log мог к ним обратиться)
    static std::string getCurrentInput();
    static std::mutex& getMutex();

private:
    // Скрываем внутреннее состояние от остального мира
    inline static std::mutex s_mutex;
    inline static std::string s_currentInput;
};

#endif // CONSOLE_H
