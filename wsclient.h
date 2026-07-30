#pragma once

#ifndef WSCLIENT_H
#define WSCLIENT_H

#include <chrono>
#include <vector>
#include <queue>
#include <ctime>
#include <cstdint>
#include <cstring>
#include <cstdlib>

#include "msgparser.h"

// Расширения C++ должны знать, что libwebsockets и msgpack — это Си-библиотеки
extern "C" {
#include <libwebsockets.h>
//#include <msgpack.h>
}

// Константы протокола QoS 2
#define MAX_RESEND_TRIES 5
#define BUFFER_CHECK_INTERVAL_MS 500
#define RESEND_CHECK_INTERVAL_MS 1000

// Структура для сообщения, хранящегося в очереди
struct BufferedMessage {
    std::vector<uint8_t> payload; // Вектор байт, выделяемый автоматически
    //std::vector<MsgParser::PuhegUpperMessage> outgoing;
};

class WSclient {
public:
    // Переменные состояния, вынесенные на уровень класса для управления из main
    struct lws *wsi = nullptr;
    int running = 1;

    // Прототипы функций управления клиентом
    void initContext();
    void sendSuccess(struct lws *wsi);

    //void sendMessage(const std::vector<uint8_t>& payload);
    void sendMessage(MsgParser::PuhegUpperMessage*);

    void processTimers();

    // 1. Статический метод-трамплин, который примет вызов от Си-библиотеки libwebsockets
    static int callbackQos2Client(struct lws *wsi, enum lws_callback_reasons reason,
                                  void *user, void *in, size_t len);

    void sendFlatCommand(const std::string& flat_line);

private:
    // 2. Внутренний метод класса, куда статический трамплин перенаправит обработку событий
    int handleCallback(struct lws *wsi, enum lws_callback_reasons reason,
                       void *user, void *in, size_t len);

    // Состояние дедупликации (последний обработанный nonce)
    int64_t last_nnc = -1;

    // Контроль недоставленного сообщения (QoS 2)
    // Вектор содержит LWS_PRE байт оверхеда в начале + сами MsgPack данные
    std::vector<uint8_t> sent_message;
    int resend_trys = 0;

    // Очередь FIFO для новых отправляемых сообщений на основе стандартного контейнера C++
    std::queue<BufferedMessage> message_buffer;

    // Метки времени для неблокирующих таймеров
    //std::clock_t last_buffer_check = 0;
    //std::clock_t last_resend_check = 0;
    std::chrono::steady_clock::time_point last_buffer_check;
    std::chrono::steady_clock::time_point last_resend_check;

};

#endif // WSCLIENT_H

