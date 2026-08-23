#include "TCPclient.h"
#include "log.h"
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/tcp.h>

void TCPclient::initContext() {
    this->socketFd_ = -1;
    this->running = 1;
    this->lastNnc = -1;
    //this->resendTries = 0;
    //this->isLineBusy = false;

    std::queue<MsgParser::PuhegUpperMessage> empty;
    std::swap(this->messageBuffer, empty);
    //this->sentMessage = MsgParser::PuhegUpperMessage();

    this->lastBufferCheck = std::chrono::steady_clock::now();
    this->lastResendCheck = std::chrono::steady_clock::now();
    this->lastTimeoutCheck = std::chrono::steady_clock::now();

    this->lastReceiveTime = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    this->sessionAlive = false;
    this->lastPongTime = std::chrono::steady_clock::now();
    this->lastPingCheck = std::chrono::steady_clock::now();

    this->readBuffer.clear();
    this->expectedLen = 0;
}

#include <netdb.h>   // getaddrinfo

bool TCPclient::connectTCP(const std::string& address) {
    const int port = 80;   // захардкожено
    this->serverAddress_ = address;
    this->serverPort_ = port;

    socketFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFd_ < 0) {
        Log::error("TCPclient", "Не удалось создать сокет");
        return false;
    }

    // keepalive (как было)
    int optval = 1;
    //int nodelay = 1;
    //setsockopt(socketFd_, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
    setsockopt(socketFd_, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
    int keepidle = 10, keepintvl = 3, keepcnt = 3;
    setsockopt(socketFd_, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
    setsockopt(socketFd_, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
    setsockopt(socketFd_, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));

    // === РЕЗОЛВ ИМЕНИ ===

    struct addrinfo hints, *res = nullptr;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int status = getaddrinfo(address.c_str(), "80", &hints, &res);
    if (status != 0 || res == nullptr) {
        Log::error("TCPclient", "Не удалось разрешить: ", address,
                   " (", gai_strerror(status), ")");
        if (res) freeaddrinfo(res);
        close(socketFd_);
        socketFd_ = -1;
        return false;
    }

    char ipbuf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &((struct sockaddr_in*)res->ai_addr)->sin_addr, ipbuf, sizeof(ipbuf));
    Log::info("TCPclient", "Разрешено ", address, " -> ", ipbuf);

    // === РЕЗОЛВ ИМЕНИ ===

    // non-blocking
    int flags = fcntl(socketFd_, F_GETFL, 0);
    fcntl(socketFd_, F_SETFL, flags | O_NONBLOCK);

    int result = connect(socketFd_, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    if (result < 0 && errno == EINPROGRESS) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(socketFd_, &wfds);
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;

        int sel = select(socketFd_ + 1, nullptr, &wfds, nullptr, &tv);
        if (sel <= 0) {
            Log::error("TCPclient", "Таймаут подключения к ", address);
            close(socketFd_);
            socketFd_ = -1;
            return false;
        }

        int so_error = 0;
        socklen_t slen = sizeof(so_error);
        getsockopt(socketFd_, SOL_SOCKET, SO_ERROR, &so_error, &slen);
        if (so_error != 0) {
            Log::error("TCPclient", "Ошибка подключения: ", strerror(so_error));
            close(socketFd_);
            socketFd_ = -1;
            return false;
        }
    } else if (result < 0) {
        Log::error("TCPclient", "Ошибка подключения: ", strerror(errno));
        close(socketFd_);
        socketFd_ = -1;
        return false;
    }

    this->sessionAlive = true;
    this->lastPongTime = std::chrono::steady_clock::now();
    Log::info("TCPclient", "Подключено к ", address, ":", port);

    if (readerThread_.joinable()) readerThread_.join();
    readerThread_ = std::thread(&TCPclient::readerLoop, this);

    return true;
}

void TCPclient::service(int timeout_ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
}

void TCPclient::destroyContext() {
    if (socketFd_ >= 0) {
        std::lock_guard<std::mutex> lock(socketMutex_);
        close(socketFd_);
        socketFd_ = -1;
        Log::info("TCPclient", "Сокет закрыт");
    }

    // ВАЖНО: НЕ трогаем this->running!
    // Внешний цикл реконнекта в main крутится по running,
    // сбрасывает его только initContext() / осознанный выход.

    if (readerThread_.joinable()) {
        readerThread_.join();
    }
}

void TCPclient::readerLoop() {

    while (this->running && socketFd_ >= 0) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(socketFd_, &readfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;

        int activity = select(socketFd_ + 1, &readfds, nullptr, nullptr, &tv);

        if (activity < 0) {
            if (errno == EINTR) continue;
            Log::error("TCPclient", "Ошибка select: ", strerror(errno));
            break;
        }

        if (activity == 0) continue;

        if (FD_ISSET(socketFd_, &readfds)) {
            uint8_t buf[4096];
            ssize_t bytesReceived;

            {
                std::lock_guard<std::mutex> lock(socketMutex_);
                bytesReceived = recv(socketFd_, buf, sizeof(buf), 0);
            }

            if (bytesReceived <= 0) {
                if (bytesReceived == 0) {
                    Log::warn("TCPclient", "Соединение закрыто сервером");
                } else {
                    Log::error("TCPclient", "Ошибка чтения: ", strerror(errno));
                }
                this->sessionAlive = false;
                break;
            }

            readBuffer.insert(readBuffer.end(), buf, buf + bytesReceived);
            this->lastReceiveTime = std::chrono::steady_clock::now();
            this->lastPongTime = std::chrono::steady_clock::now();

            while (readBuffer.size() >= 4) {
                if (expectedLen == 0) {
                    expectedLen = (readBuffer[0] << 24) |
                                  (readBuffer[1] << 16) |
                                  (readBuffer[2] << 8) |
                                  readBuffer[3];
                    readBuffer.erase(readBuffer.begin(), readBuffer.begin() + 4);

                    if (expectedLen == 0 || expectedLen > 65536) {
                        Log::error("TCPclient", "Неверная длина: ", expectedLen);
                        readBuffer.clear();
                        expectedLen = 0;
                        continue;
                    }
                }

                if (readBuffer.size() < expectedLen) {
                    break;
                }

                std::vector<uint8_t> frame(readBuffer.begin(),
                                           readBuffer.begin() + expectedLen);
                readBuffer.erase(readBuffer.begin(), readBuffer.begin() + expectedLen);
                expectedLen = 0;

                /*if (frame.size() == 1 && frame[0] == 'S') {
                    // подтверждение доставки
                    successReceived();

                } else */
                if (frame.size() == 1 && frame[0] == 'P') {
                    // pong от станции: признак жизни, НЕ данные
                    this->lastPongTime = std::chrono::steady_clock::now();

                } else {
                    // боевые данные
                    //if (this->isLineBusy) {
                        //this->isLineBusy = false;
                        //this->sentMessage = MsgParser::PuhegUpperMessage();
                        //this->resendTries = 0;
                        //Log::info("TCPclient", "Получены данные. Линия свободна.");
                    //}

                    //sendSuccess();

                    try {
                        msgpack::object_handle oh = msgpack::unpack(
                            reinterpret_cast<const char*>(frame.data()), frame.size());
                        msgpack::object deserialized = oh.get();

                        int64_t current_nnc = extractNnc(deserialized);
                        if (current_nnc != -1) {
                            if (current_nnc == this->lastNnc) {
                                continue;  // дубликат
                            }
                            this->lastNnc = current_nnc;
                        }

                        MsgParser::dispatchIncomingPacket(deserialized);
                    } catch (const std::exception& e) {
                        Log::error("TCPclient", "Ошибка десериализации: ", e.what());
                    }
                }
                /*std::vector<uint8_t> frame(readBuffer.begin(),
                                           readBuffer.begin() + expectedLen);
                readBuffer.erase(readBuffer.begin(), readBuffer.begin() + expectedLen);
                expectedLen = 0;

                if (frame.size() == 1 && frame[0] == 'S') {
                    successReceived();
                } else {
                    if (this->isLineBusy) {
                        this->isLineBusy = false;
                        this->sentMessage = MsgParser::PuhegUpperMessage();
                        this->resendTries = 0;
                        Log::info("TCPclient", "Получены данные. Линия свободна.");
                    }

                    sendSuccess();

                    try {
                        msgpack::object_handle oh = msgpack::unpack(
                            reinterpret_cast<const char*>(frame.data()), frame.size());
                        msgpack::object deserialized = oh.get();

                        int64_t current_nnc = extractNnc(deserialized);
                        if (current_nnc != -1) {
                            if (current_nnc == this->lastNnc) {
                                continue;
                            }
                            this->lastNnc = current_nnc;
                        }

                        MsgParser::dispatchIncomingPacket(deserialized);
                    } catch (const std::exception& e) {
                        Log::error("TCPclient", "Ошибка десериализации: ", e.what());
                    }

                }*/
            }
        }
    }

    if (!this->sessionAlive) {
        std::lock_guard<std::mutex> lock(socketMutex_);
        if (socketFd_ >= 0) {
            close(socketFd_);
            socketFd_ = -1;
        }
    }
}

void TCPclient::sendWithLength(const std::vector<uint8_t>& data) {
    if (socketFd_ < 0) return;

    uint32_t len = data.size();
    uint8_t header[4] = {
        (uint8_t)(len >> 24),
        (uint8_t)(len >> 16),
        (uint8_t)(len >> 8),
        (uint8_t)len
    };

    std::lock_guard<std::mutex> lock(socketMutex_);
    ssize_t sent = send(socketFd_, header, 4, MSG_NOSIGNAL);
    if (sent != 4) {
        Log::error("TCPclient", "Ошибка отправки заголовка");
        return;
    }

    sent = send(socketFd_, data.data(), len, MSG_NOSIGNAL);
    if (sent != (ssize_t)len) {
        Log::error("TCPclient", "Ошибка отправки данных: отправлено ", sent, " из ", len);
    }
}

/*void TCPclient::sendSuccess() {
    std::vector<uint8_t> ack = {'S'};
    sendWithLength(ack);
}*/

/*
void TCPclient::successReceived() {
    if (this->isLineBusy) {
        this->isLineBusy = false;
        this->sentMessage = MsgParser::PuhegUpperMessage();
        //this->resendTries = 0;
    }
}*/

void TCPclient::sendMessage(MsgParser::PuhegUpperMessage* pumsg, bool forceSend) {
    if (!forceSend) {
        auto sinceRx = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - this->lastReceiveTime).count();
        if (sinceRx < POST_RX_GUARD_MS) {
            Log::info("TCPclient", "ОЖИДАНИЕ: ", sinceRx, " мс после приёма. Пакет в очередь.");
            this->messageBuffer.push(*pumsg);
            return;
        }
    }

    bool isDeviceBusy = MsgParser::isDeviceBusy();

    //if (!forceSend && (this->isLineBusy || this->socketFd_ < 0 || isDeviceBusy || MsgParser::isRxBusy())) {
    if (!forceSend && (this->socketFd_ < 0 || isDeviceBusy || MsgParser::isRxBusy())) {
        if (this->socketFd_ < 0) {
            Log::error("TCPclient", "Сокет не подключен");
        } else if (isDeviceBusy) {
            Log::info("TCPclient", "Устройство занято. Пакет в буфер.");
        } else if (MsgParser::isRxBusy()) {
            Log::info("TCPclient", "Входящий транспорт. Пакет в буфер.");
        }

        const size_t MAX_QUEUE = 64;
        if (this->messageBuffer.size() >= MAX_QUEUE) {
            this->messageBuffer.pop();
        }
        this->messageBuffer.push(*pumsg);
        return;
    }

    if (!isDeviceBusy) {
        MsgParser::startProcess(pumsg->thread);
    }

    sendWithLength(pumsg->packedMsg);
    Log::info("TCPclient", "Сообщение отправлено (", pumsg->packedMsg.size(), " байт)");

    this->sentMessage = *pumsg;
    //this->isLineBusy = true;

    //if (!forceSend) {
    //    this->resendTries = 0;
    //}
}

void TCPclient::processTimers() {
    auto currentTime = std::chrono::steady_clock::now();

    // === KEEPALIVE ===
    if (this->sessionAlive && this->socketFd_ >= 0) {
        // пинг 'P' каждые 3 секунды
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                currentTime - this->lastPingCheck).count() >= PING_INTERVAL_MS) {
            this->lastPingCheck = currentTime;
            std::vector<uint8_t> ping = {'P'};
            sendWithLength(ping);
        }

        // контроль жизни: 10 секунд тишины = разрыв
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                currentTime - this->lastPongTime).count() >= KEEPALIVE_TIMEOUT_MS) {
            Log::warn("TCPclient", "Нет ответа > ", KEEPALIVE_TIMEOUT_MS, " мс. Разрыв.");
            this->sessionAlive = false;
            std::lock_guard<std::mutex> lock(socketMutex_);
            if (socketFd_ >= 0) {
                close(socketFd_);
                socketFd_ = -1;
            }
            return;
        }
    }
    // === конец keepalive ===

    auto passedBufferMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                              currentTime - this->lastBufferCheck).count();

    if (passedBufferMs >= 1000) {
        this->lastBufferCheck = currentTime;

        if (this->socketFd_ >= 0 &&
            !this->messageBuffer.empty() &&
            !MsgParser::isDeviceBusy() &&
            !MsgParser::isRxBusy()) {

            auto sinceRx = std::chrono::duration_cast<std::chrono::milliseconds>(
                               currentTime - this->lastReceiveTime).count();
            if (sinceRx >= POST_RX_GUARD_MS) {
                MsgParser::PuhegUpperMessage nextMsg = this->messageBuffer.front();
                this->messageBuffer.pop();
                sendMessage(&nextMsg, false);
            }
        }
    }

    /*
    auto passedResendMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                              currentTime - this->lastResendCheck).count();
    if (passedResendMs >= PASSED_RESEND_MS) {
        this->lastResendCheck = currentTime;
        if (this->isLineBusy && this->socketFd_ >= 0 && this->sentMessage.id != 0) {
            if (this->resendTries < 3) {
                this->resendTries++;
                sendMessage(&this->sentMessage, true);
            } else {
                Log::error("TCPclient", "Превышено число попыток");
                this->isLineBusy = false;
                this->sentMessage = MsgParser::PuhegUpperMessage();
                this->resendTries = 0;
            }
        }
    }*/

    static std::chrono::steady_clock::time_point lastTimeoutCheck = std::chrono::steady_clock::now();
    auto passedTimeoutMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               currentTime - lastTimeoutCheck).count();
    if (passedTimeoutMs >= PASSED_TIMEOUT_MS) {
        lastTimeoutCheck = currentTime;
        MsgParser::checkProcessTimeout();
        MsgParser::checkRxTimeout();
    }
}

int64_t TCPclient::extractNnc(const msgpack::object& obj) {
    if (obj.type != msgpack::type::MAP) return -1;
    std::map<std::string, msgpack::object> root_map;
    obj.convert(root_map);
    for (const auto& pair : root_map) {
        if (pair.second.type == msgpack::type::MAP) {
            std::map<std::string, msgpack::object> inner_map;
            pair.second.convert(inner_map);
            auto it = inner_map.find("nnc");
            if (it != inner_map.end()) {
                return it->second.as<int64_t>();
            }
        }
    }
    return -1;
}
