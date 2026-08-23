#include "tun.h"
//#include "TCPclient.h"
#include "TCPclient.h"
#include "msgparser.h"
#include "log.h"
#include "addressbook.h"

#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <sys/ioctl.h>
#include <cerrno>
#include <iostream>
#include <cstdlib>

// ВАЖНО: Сначала системные заголовки, потом linux-специфичные
#include <arpa/inet.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <net/if.h>        // <-- Сначала этот
#include <linux/if.h>      // <-- Потом этот (чтобы избежать конфликтов)
#include <linux/if_tun.h>

TunInterface::TunInterface() {}

TunInterface::~TunInterface() {
    stop();
    close();
}

std::string TunInterface::ipToString(uint32_t ip) {
    struct in_addr addr;
    addr.s_addr = ip;
    char str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr, str, INET_ADDRSTRLEN);
    return std::string(str);
}

bool TunInterface::findByIp(uint32_t targetIp) {
    std::string expectedIpStr = ipToString(targetIp);
    Log::info("TUN", "Ищем интерфейс с IP: ", expectedIpStr);

    struct ifaddrs *ifaddr, *ifa;

    if (getifaddrs(&ifaddr) == -1) {
        Log::error("TUN", "getifaddrs failed");
        return false;
    }

    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;

        if (strncmp(ifa->ifa_name, "tun", 3) != 0) continue;

        struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;

        if (addr->sin_addr.s_addr == targetIp) {
            Log::info("TUN", "Найден интерфейс: ", ifa->ifa_name, " с IP ", expectedIpStr);
            freeifaddrs(ifaddr);
            return open(ifa->ifa_name);
        }
    }

    freeifaddrs(ifaddr);
    Log::info("TUN", "Интерфейс с IP ", expectedIpStr, " не найден");
    return false;
}

bool TunInterface::createWithIp(const std::string& ifname, uint32_t ip,
                                int netmask, int mtu) {
    std::string ipStr = ipToString(ip);
    Log::info("TUN", "Создаем интерфейс ", ifname, " с IP ", ipStr, "/", netmask);

    const char* user = getenv("USER");
    if (!user) user = "root";

    std::string cmd = "ip tuntap add dev " + ifname + " mode tun user " + user;
    if (system(cmd.c_str()) != 0) {
        Log::error("TUN", "Не удалось создать интерфейс. Возможно, нужны права root.");
        return false;
    }

    cmd = "ip addr add " + ipStr + "/" + std::to_string(netmask) + " dev " + ifname;
    if (system(cmd.c_str()) != 0) {
        Log::error("TUN", "Не удалось назначить IP");
        return false;
    }

    cmd = "ip link set " + ifname + " mtu " + std::to_string(mtu);
    system(cmd.c_str());

    cmd = "ip link set " + ifname + " up";
    if (system(cmd.c_str()) != 0) {
        Log::error("TUN", "Не удалось поднять интерфейс");
        return false;
    }

    //запрет IP6
    cmd = "sysctl -w net.ipv6.conf." + ifname + ".disable_ipv6=1";
    system(cmd.c_str());

    Log::info("TUN", "Интерфейс ", ifname, " создан и настроен");
    return open(ifname);
}

bool TunInterface::init(uint32_t ip, const std::string& ifname,
                        int netmask, int mtu) {
    std::string ipStr = ipToString(ip);
    Log::info("TUN", "Инициализация TUN для IP: ", ipStr);

    if (findByIp(ip)) {
        Log::info("TUN", "✅ Найден существующий TUN-интерфейс: ", ifname_);
        return true;
    }

    Log::info("TUN", "Попытка создать TUN-интерфейс автоматически...");
    if (createWithIp(ifname, ip, netmask, mtu)) {
        Log::info("TUN", "✅ TUN-интерфейс создан: ", ifname_);
        return true;
    }

    Log::error("TUN", "❌ Не удалось создать TUN автоматически.");
    Log::error("TUN", "Выполните следующие команды вручную:");
    Log::error("TUN", "  sudo ip tuntap add dev ", ifname, " mode tun user $(whoami)");
    Log::error("TUN", "  sudo ip addr add ", ipStr, "/", netmask, " dev ", ifname);
    Log::error("TUN", "  sudo ip link set ", ifname, " mtu ", mtu);
    Log::error("TUN", "  sudo ip link set ", ifname, " up");
    Log::error("TUN", "  sudo sysctl -w net.ipv6.conf.", ifname, ".disable_ipv6=1");
    Log::error("TUN", "Затем перезапустите программу.");

    return false;
}

void TunInterface::start(TCPclient* client) {
    if (!isOpen()) {
        Log::info("TUN", "Невозможно запустить: интерфейс не открыт");
        return;
    }

    if (readerThread_.joinable()) {
        Log::info("TUN", "Поток чтения уже запущен");
        return;
    }

    running_ = true;
    readerThread_ = std::thread(&TunInterface::readerThread, this, client);
    Log::info("TUN", "Поток чтения пакетов запущен");
}

void TunInterface::stop() {
    if (running_) {
        running_ = false;
    }

    if (readerThread_.joinable()) {
        readerThread_.join();
        Log::info("TUN", "Поток чтения остановлен");
    }
}

void TunInterface::readerThread(TCPclient* client) {
    Log::info("TUN", "Поток чтения TUN запущен.");

    std::vector<uint8_t> packet;

    while (running_ && client->running) {

        // Не читаем TUN, пока станция принимает или идёт наша транзакция:
        // пакеты ОС пусть копятся в ядерной очереди (естественный flow control)
        if (MsgParser::isRxBusy() || MsgParser::isDeviceBusy()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        if (readPacket(packet)) {
            processPacket(packet, client);
        }
    }

    Log::info("TUN", "Поток чтения TUN завершен.");
}

void TunInterface::processPacket(const std::vector<uint8_t>& packet, TCPclient* client) {
    if (packet.size() < 20) return;

    // ФИЛЬТР 1: только IPv4
    uint8_t version = packet[0] >> 4;
    if (version != 4) return;

    // Вычисляем целевой MAC из IP назначения
    uint16_t destMac = (packet[18] << 8) | packet[19];

    // ФИЛЬТР 2: только известные узлы из адресной книги
    Addressbook::Contact target;
    if (!Addressbook::getInstance().getContact(destMac, target)) {
        return;
    }

    // === ОБЕРТКА В СООБЩЕНИЕ ВЕРХНЕГО УРОВНЯ PUHEG ===
    MsgParser::PuhegUpperMessage pumsg;
    pumsg.what = "toss";                 // команда передачи данных
    pumsg.todo = "";
    pumsg.howmuch = destMac;             // целевой MAC
    pumsg.msg.assign(packet.begin(), packet.end()); // сырой IP-пакет как полезная нагрузка

    // Сериализуем в msgpack (id и thread проставятся внутри)
    parser.packMessage(&pumsg);

    // Отправляем через QoS2-механизм (очередь, ресенды)
    client->sendMessage(&pumsg);

    Log::info("TUN", "📦 IP-пакет ", packet.size(), " байт обернут в toss для MAC ", destMac);
}

bool TunInterface::open(const std::string& ifname) {
    ifname_ = ifname;

    fd_ = ::open("/dev/net/tun", O_RDWR);
    if (fd_ < 0) {
        Log::error("TUN", "Не удалось открыть /dev/net/tun. Нужны права root или CAP_NET_ADMIN.");
        return false;
    }

    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));

    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;

    if (ifname.length() >= sizeof(ifr.ifr_name)) {
        Log::error("TUN", "Имя интерфейса слишком длинное");
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    std::strncpy(ifr.ifr_name, ifname.c_str(), sizeof(ifr.ifr_name) - 1);

    if (ioctl(fd_, TUNSETIFF, (void*)&ifr) < 0) {
        Log::error("TUN", "ioctl TUNSETIFF failed: ", std::strerror(errno));
        ::close(fd_);
        fd_ = -1;
        return false;
    }

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

    packet.resize(2048);

    ssize_t len = ::read(fd_, packet.data(), packet.size());

    if (len > 0) {
        packet.resize(len);
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
