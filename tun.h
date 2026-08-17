#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <thread>
#include <atomic>

class WSclient;

class TunInterface {
public:
    TunInterface();
    ~TunInterface();

    bool open(const std::string& ifname);
    void close();
    bool readPacket(std::vector<uint8_t>& packet);
    bool writePacket(const uint8_t* data, size_t len);
    int getFd() const { return fd_; }
    bool isOpen() const { return fd_ >= 0; }
    std::string getIfname() const { return ifname_; }

    bool init(uint32_t ip, const std::string& ifname = "tun0",
              int netmask = 24, int mtu = 1400);

    void start(WSclient* client);
    void stop();

    static std::string ipToString(uint32_t ip);

private:
    int fd_ = -1;
    std::string ifname_;
    std::thread readerThread_;
    std::atomic<bool> running_{false};

    void readerThread(WSclient* client);  // <-- private метод
    //void processPacket(const std::vector<uint8_t>& packet);
    void processPacket(const std::vector<uint8_t>& packet, WSclient* client); // <-- + client


    bool findByIp(uint32_t targetIp);
    bool createWithIp(const std::string& ifname, uint32_t ip,
                      int netmask, int mtu);


};
