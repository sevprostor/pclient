#include "tun.h"
#include "log.h"

#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <cerrno>
#include <iostream>

TunInterface::TunInterface() {}

TunInterface::~TunInterface() {
    close();
}

// Поток чтения из TUN (метод класса, используем this)
void TunInterface::tunReaderThread(WSclient* client) {
    Log::info("TUN", "Поток чтения TUN запущен.");

    std::vector<uint8_t> packet;

    while (client->running) {
        if (this->readPacket(packet)) {  // <-- ИСПРАВЛЕНО: this-> вместо tun.
            // Парсим Destination IP (байты 16, 17, 18, 19 в IPv4 заголовке)
            if (packet.size() >= 20) {
                uint8_t ip1 = packet[16];
                uint8_t ip2 = packet[17];
                uint8_t ip3 = packet[18];
                uint8_t ip4 = packet[19];

                // Вычисляем MAC по вашей формуле
                uint16_t mac = (ip3 << 8) | ip4;

                Log::info("TUN", "Пакет ", packet.size(), " байт. Dest IP: ",
                          ip1, ".", ip2, ".", ip3, ".", ip4, " -> MAC: ", mac);
            }
        }
    }
}

bool TunInterface::open(const std::string& ifname) {
    ifname_ = ifname;

    // 1. Открываем устройство TUN
    fd_ = ::open("/dev/net/tun", O_RDWR);
    if (fd_ < 0) {
        Log::error("TUN", "Не удалось открыть /dev/net/tun. Нужны права root или CAP_NET_ADMIN.");
        return false;
    }

    // 2. Настраиваем интерфейс
    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));

    // IFF_TUN - режим TUN (только IP пакеты, без Ethernet заголовков)
    // IFF_NO_PI - не добавлять Packet Information (лишние 4 байта в начале)
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;

    // Копируем имя интерфейса (макс 16 символов)
    if (ifname.length() >= sizeof(ifr.ifr_name)) {
        Log::error("TUN", "Имя интерфейса слишком длинное");
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    std::strncpy(ifr.ifr_name, ifname.c_str(), sizeof(ifr.ifr_name) - 1);

    // 3. Создаем интерфейс через ioctl
    if (ioctl(fd_, TUNSETIFF, (void*)&ifr) < 0) {
        Log::error("TUN", "ioctl TUNSETIFF failed: ", std::strerror(errno));
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    // Сохраняем реальное имя (ядро могло его немного изменить)
    ifname_ = ifr.ifr_name;
    Log::info("TUN", "Интерфейс ", ifname_, " успешно открыт.");
    return true;
}

void TunInterface::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
        Log::info("TUN", "Интерфейс закрыт.");
    }
}

bool TunInterface::readPacket(std::vector<uint8_t>& packet) {
    if (fd_ < 0) return false;

    // Стандартный MTU для Ethernet + запас на заголовки
    packet.resize(2048);

    ssize_t len = ::read(fd_, packet.data(), packet.size());

    if (len > 0) {
        packet.resize(len); // Обрезаем вектор до реального размера пакета
        return true;
    }

    if (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        Log::error("TUN", "Ошибка чтения из TUN: ", std::strerror(errno));
    }

    return false;
}

bool TunInterface::writePacket(const uint8_t* data, size_t len) {
    if (fd_ < 0) return false;

    ssize_t written = ::write(fd_, data, len);
    return written == static_cast<ssize_t>(len);
}
