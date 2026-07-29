#pragma once

#ifndef MSGPARSER_H
#define MSGPARSER_H

#include <vector>
#include <iostream>
#include <string>
#include <map>
#include <cstdint>

// Подключаем C++ интерфейс MsgPack
#include <msgpack.hpp>

// libwebsockets остается Си-библиотекой
extern "C" {
#include <libwebsockets.h>
}

class MsgParser {
public:

    struct PuhegUpperMessage{

        std::string what;
        std::string todo;
        int32_t howmuch = -1;
        std::vector<uint8_t> msg;
        uint32_t thread = 0;

    };

    // Принимает плоскую строку и возвращает упакованный MsgPack-вектор с LWS_PRE оверхедом
    static std::vector<uint8_t> parseFlatCommand(const std::string& flat_line);

    // Красивый вывод MsgPack объекта на экран
    static void printPretty(const msgpack::object& obj, int indent = 0);

    // Извлечение nnc (nonce) из входящего бинарного буфера
    static int64_t extractNnc(const uint8_t *data, size_t size);

    // Упаковка исходящего сообщения на чистом C++ (динамический подсчет полей)
    // Изменено: howmuch теперь int32_t (для поддержки -1), msg принимает вектор байт
    /*
    static std::vector<uint8_t> packMessage(const std::string& what,
                                            const std::string& todo,
                                            int32_t howmuch,
                                            const std::vector<uint8_t>& msg);
    */

    static std::vector<uint8_t> packMessage(PuhegUpperMessage*);

    // Метод диспетчеризации входящих системных MsgPack-объектов
    static void dispatchIncomingPacket(const msgpack::object& obj);
};

#endif // MSGPARSER_H
