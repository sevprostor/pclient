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

        uint32_t id;
        std::string what;
        std::string todo;
        int32_t howmuch = -1;
        std::vector<uint8_t> msg;
        std::vector<uint8_t> packedMsg;
        uint32_t thread = 0;

        MSGPACK_DEFINE_MAP(id, what, todo, howmuch, msg, thread);

    };

    struct RunningProcess{
        uint32_t thread;
        std::string state;
        bool running = false;
    };

    // Принимает плоскую строку и возвращает упакованный MsgPack-вектор с LWS_PRE оверхедом
    //static std::vector<uint8_t> parseFlatCommand(const std::string& flat_line);
    void parseFlatCommand(const std::string& flat_line, PuhegUpperMessage*);

    // Извлечение nnc (nonce) из входящего бинарного буфера
    static int64_t extractNnc(const uint8_t *data, size_t size);

    void packMessage(PuhegUpperMessage*);

    // Метод диспетчеризации входящих системных MsgPack-объектов
    static void dispatchIncomingPacket(const msgpack::object& obj);

    // --- Управление состоянием процесса ---
    static bool isDeviceBusy();
    static void startProcess(uint32_t threadId);
    static void watchProcess(const std::string& state, const std::string& thread);

};

extern MsgParser parser;
extern MsgParser::RunningProcess runningProc;

#endif // MSGPARSER_H
