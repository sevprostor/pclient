#ifndef COMMANDER_H
#define COMMANDER_H

#pragma once
#include <string>
#include <cstdint>
#include <cstddef>

class Commander {
public:
    static constexpr const char* PREFIX = ">>> ";
    static constexpr size_t PREFIX_LEN = 4;

    // начинается ли с ">>> "
    static bool isCommand(const uint8_t* data, size_t len);
    static bool isCommand(const std::string& s);

    // входная точка: строка УЖЕ без префикса
    static void handle(const std::string& body, const std::string& source);
};

#endif // COMMANDER_H
