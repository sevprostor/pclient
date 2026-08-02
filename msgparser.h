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
        std::vector<int> msg; //Это должно отправляться как массив байт JSON
        std::vector<uint8_t> packedMsg;
        uint32_t thread = 0;

        MSGPACK_DEFINE_MAP(id, what, todo, howmuch, msg, thread);

    };

    struct RunningProcess{
        uint32_t thread;
        std::string state;
        bool running = false;
        std::chrono::steady_clock::time_point startTime; // <-- ДОБАВЛЕНО
        std::chrono::steady_clock::time_point lastResponseTime; // <-- ДОБАВЛЕНО
        bool justLaunched = true;
    };

    //Кодировка msg в JsonArray
    std::vector<int> encodeMsg(const std::string& text);
    std::vector<int> encodeMsg(const std::vector<uint8_t>& rawBytes);

    // Принимает плоскую строку и возвращает упакованный MsgPack-вектор с LWS_PRE оверхедом
    //static std::vector<uint8_t> parseFlatCommand(const std::string& flat_line);
    void parseFlatCommand(const std::string& flat_line, PuhegUpperMessage*);

    // Извлечение nnc (nonce) из входящего бинарного буфера
    //static int64_t extractNnc(const uint8_t *data, size_t size);

    void packMessage(PuhegUpperMessage*);

    // Метод диспетчеризации входящих системных MsgPack-объектов
    static void dispatchIncomingPacket(const msgpack::object& obj);

    // --- Управление состоянием процесса ---
    static bool isDeviceBusy();
    static void startProcess(uint32_t threadId);
    static void watchProcess(const std::string& state, const std::string& thread);
    static void checkProcessTimeout(); // <-- ДОБАВЛЕНО

};

extern MsgParser parser;
extern MsgParser::RunningProcess runningProc;

#endif // MSGPARSER_H
