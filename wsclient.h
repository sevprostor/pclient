#pragma once
#include <string>
#include <queue>
#include <chrono>
#include <vector>
#include <libwebsockets.h>
#include "msgparser.h"

class WSclient {
public:

    //Это все про проверку подключения вебсокета
    std::atomic<bool> sessionAlive{false};
    std::chrono::steady_clock::time_point lastPongTime;   // единственный счётчик жизни
    std::chrono::steady_clock::time_point lastPingCheck;
    static constexpr int PING_INTERVAL_MS   = 3000;
    static constexpr int KEEPALIVE_TIMEOUT_MS = 10000;
    static constexpr int PASSED_TIMEOUT_MS   = 500;
    static constexpr int PASSED_RESEND_MS   = 2000;
    static constexpr int POST_RX_GUARD_MS = 2000; // Защитный интервал после приёма (мс), раньше не отправляем

    bool isConnected() const { return wsi != nullptr; }

    void initContext();
    bool connectWS(const std::string& wsAddr); // <-- Изменили на const ссылку

    void service(int timeout_ms);
    void destroyContext();

    void sendMessage(MsgParser::PuhegUpperMessage *pumsg, bool forceSend = false);
    void processTimers();

    static int callbackQos2Client(struct lws *wsi, enum lws_callback_reasons reason,
                                  void *user, void *in, size_t len);

    int running = 1;

private:
    struct lws_context* lws_ctx_ = nullptr; // <-- ОБЯЗАТЕЛЬНО ЗДЕСЬ
    struct lws* wsi = nullptr;

    int64_t lastNnc = -1;
    int resendTries = 0;
    bool isLineBusy = false;

    std::queue<MsgParser::PuhegUpperMessage> messageBuffer;
    MsgParser::PuhegUpperMessage sentMessage;

    std::chrono::steady_clock::time_point lastBufferCheck;
    std::chrono::steady_clock::time_point lastResendCheck;
    std::chrono::steady_clock::time_point lastTimeoutCheck;

    // Время последнего получения сообщения от станции
    std::chrono::steady_clock::time_point lastReceiveTime;



    void sendSuccess(struct lws *wsiParam);
    void successReceived();
    int64_t extractNnc(const msgpack::object& obj);
};
