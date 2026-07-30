#pragma once
#include <iostream>
#include <vector>
#include <queue>
#include <string>
#include <chrono>
#include <libwebsockets.h>
#include "msgparser.h"

class WSclient {
public:
    // Состояние
    struct lws *wsi;
    int running;
    int lastNnc;
    int resendTries;

    // Главный семафор и буфер
    bool isLineBusy;
    std::queue<MsgParser::PuhegUpperMessage> messageBuffer;
    MsgParser::PuhegUpperMessage sentMessage;

    // Таймеры
    std::chrono::steady_clock::time_point lastBufferCheck;
    std::chrono::steady_clock::time_point lastResendCheck;

    // Методы
    void initContext();
    void sendSuccess(struct lws *wsiParam);
    void sendMessage(MsgParser::PuhegUpperMessage *pumsg, bool forceSend = false);
    void processTimers();
    void successReceived();

    // Статический callback для libwebsockets
    static int callbackQos2Client(struct lws *wsi, enum lws_callback_reasons reason,
                                  void *user, void *in, size_t len);
};
