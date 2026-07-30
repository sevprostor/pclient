#ifndef LOG_H
#define LOG_H

#pragma once
#include <iostream>
#include <string>
#include <chrono>
#include <iomanip>

class Log {
public:
    // Информационное сообщение (вывод в std::cout)
    static void info(const std::string& tag, const std::string& msg) {
        print("INFO", tag, msg, false);
    }

    // Сообщение об ошибке (вывод в std::cerr)
    static void error(const std::string& tag, const std::string& msg) {
        print("ERR", tag, msg, true);
    }

private:
    static void print(const std::string& level, const std::string& tag, const std::string& msg, bool isError) {
        // Получаем текущее время
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        // Преобразуем в локальное время (потокобезопасная версия для Linux/POSIX)
        std::tm tm_buf;
        localtime_r(&time_t_now, &tm_buf);

        // Выбираем поток вывода
        std::ostream& out = isError ? std::cerr : std::cout;

        // Форматируем и выводим: [ЧЧ:ММ:СС.мс] [УРОВЕНЬ] [ТЕГ] Сообщение
        out << "[" << std::put_time(&tm_buf, "%H:%M:%S") << "."
            << std::setfill('0') << std::setw(3) << ms.count() << "] "
            << "[" << level << "] "
            << "[" << tag << "] "
            << msg << std::endl;
    }
};

#endif // LOG_H
