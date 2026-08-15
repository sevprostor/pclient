#ifndef TUN_H
#define TUN_H

#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include "wsclient.h"

class TunInterface {
public:
    TunInterface();
    ~TunInterface();

    void tunReaderThread(WSclient* client);

    // Открывает TUN-интерфейс с заданным именем (например, "tun0")
    bool open(const std::string& ifname);

    // Закрывает интерфейс
    void close();

    // Читает один IP-пакет (блокирующая операция)
    // Возвращает true, если пакет прочитан успешно
    bool readPacket(std::vector<uint8_t>& packet);

    // Записывает IP-пакет в интерфейс (для приема из сети)
    bool writePacket(const uint8_t* data, size_t len);

    // Получить файловый дескриптор (полезно для poll/select в будущем)
    int getFd() const { return fd_; }

    bool isOpen() const { return fd_ >= 0; }

private:
    int fd_ = -1;
    std::string ifname_;
};

//extern TunInterface tun;

#endif // TUN_H
