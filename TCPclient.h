#ifndef TCPCLIENT_H
#define TCPCLIENT_H

#pragma once
#include <string>
#include <queue>
#include <chrono>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <netdb.h>
#include "msgparser.h"

class TCPclient {
public:
    std::atomic<bool> sessionAlive{false};
    std::chrono::steady_clock::time_point lastPongTime;
    std::chrono::steady_clock::time_point lastPingCheck;
    std::chrono::steady_clock::time_point lastReceiveTime;

    static constexpr int PING_INTERVAL_MS = 3000;
    static constexpr int KEEPALIVE_TIMEOUT_MS = 10000;
    static constexpr int POST_RX_GUARD_MS = 500;
    static constexpr int PASSED_TIMEOUT_MS = 500;
    //static constexpr int PASSED_RESEND_MS = 2000;

    bool isConnected() const { return socketFd_ >= 0; }

    void initContext();
    bool connectTCP(const std::string& address);
    void service(int timeout_ms);
    void destroyContext();

    void sendMessage(MsgParser::PuhegUpperMessage* pumsg, bool forceSend = false);
    void processTimers();
    void sendTimeSync();

    int running = 1;

private:
    int socketFd_ = -1;
    std::string serverAddress_;
    int serverPort_ = 80;

    int64_t lastNnc = -1;
    //int resendTries = 0;
    //bool isLineBusy = false;

    std::queue<MsgParser::PuhegUpperMessage> messageBuffer;
    MsgParser::PuhegUpperMessage sentMessage;

    std::chrono::steady_clock::time_point lastBufferCheck;
    std::chrono::steady_clock::time_point lastResendCheck;
    std::chrono::steady_clock::time_point lastTimeoutCheck;

    std::vector<uint8_t> readBuffer;
    uint32_t expectedLen = 0;

    std::thread readerThread_;
    std::mutex socketMutex_;

    void readerLoop();
    void sendWithLength(const std::vector<uint8_t>& data);
    //void sendSuccess();
    void successReceived();
    int64_t extractNnc(const msgpack::object& obj);
};

#endif // TCPCLIENT_H
