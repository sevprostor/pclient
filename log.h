#pragma once
#include "console.h"
#include <iostream>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <mutex>




// Объявляем внешние переменные для работы с консолью
extern std::mutex consoleMutex;
extern std::string g_currentInput;

class Log {
public:
    // Шаблон для INFO: принимает тег и любое количество аргументов
    template<typename... Args>
    static void info(const std::string& tag, Args&&... args) {
        print(tag, std::forward<Args>(args)...);
    }

    // Шаблон для ERROR: принимает тег и любое количество аргументов
    template<typename... Args>
    static void error(const std::string& tag, Args&&... args) {
        print(tag, std::forward<Args>(args)...);
    }

private:
    // Базовый случай рекурсии (когда аргументы закончились)
    static void buildMessage(std::ostringstream&) {}

    // Рекурсивный случай: берем первый аргумент, пишем в поток, передаем остальные
    template<typename T, typename... Args>
    static void buildMessage(std::ostringstream& ss, T&& first, Args&&... rest) {
        ss << std::forward<T>(first);
        buildMessage(ss, std::forward<Args>(rest)...);
    }

    // Основная функция печати (теперь с поддержкой перерисовки)
    template<typename... Args>

    static void print(const std::string& level, const std::string& tag, Args&&... args){
        // Получаем время
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        std::tm tm_buf;
        localtime_r(&time_t_now, &tm_buf);

        // Склеиваем все переданные аргументы
        std::ostringstream ss;
        buildMessage(ss, std::forward<Args>(args)...);

        // Блокируем консоль для атомарного вывода
        std::lock_guard<std::mutex> lock(consoleMutex);

        // Выбираем поток вывода
        std::ostream& out = (level == "ERR") ? std::cerr : std::cout;

        // 1. Возвращаем курсор в начало строки и очищаем её
        out << "\r\033[K";

        // 2. Выводим лог с меткой времени
        out << "[" << std::put_time(&tm_buf, "%H:%M:%S") << "."
            << std::setfill('0') << std::setw(3) << ms.count() << "] "
            << "[" << level << "] "
            << "[" << tag << "] "
            << ss.str() << std::endl;

        // 3. Перерисовываем приглашение и текущий ввод пользователя
        //out << "> " << g_currentInput << std::flush;
        Console::redrawPrompt();
    }
};
