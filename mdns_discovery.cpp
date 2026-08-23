#include "mdns_discovery.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <map>
#include <algorithm>
#include <chrono>

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

static std::string readName(const uint8_t* p, size_t len, size_t& off) {
    std::string out;
    int jumps = 0;
    bool jumped = false;
    size_t next = 0;
    while (off < len) {
        uint8_t l = p[off];
        if (l == 0) { off++; break; }
        if ((l & 0xC0) == 0xC0) {
            if (off + 1 >= len) break;
            uint16_t ptr = ((l & 0x3F) << 8) | p[off + 1];
            if (!jumped) next = off + 2;
            jumped = true;
            off = ptr;
            if (++jumps > 8) break;
        } else {
            off++;
            if (off + l > len) break;
            if (!out.empty()) out += '.';
            out.append((const char*)p + off, l);
            off += l;
        }
    }
    if (jumped) off = next;
    return out;
}

static std::vector<uint8_t> buildQuery() {
    std::vector<uint8_t> q(12, 0);
    q[5] = 1;                                   // QDCOUNT
    const char* labels[] = {"_puheg", "_tcp", "local"};
    for (auto lb : labels) {
        q.push_back((uint8_t)strlen(lb));
        q.insert(q.end(), lb, lb + strlen(lb));
    }
    q.push_back(0);
    q.push_back(0); q.push_back(12);            // PTR
    q.push_back(0x80); q.push_back(0x01);       // IN | unicast-response
    return q;
}

static std::string stripService(const std::string& n) {
    const std::string suf = "._puheg._tcp.local";
    if (n.size() > suf.size() &&
        toLower(n.substr(n.size() - suf.size())) == suf)
        return n.substr(0, n.size() - suf.size());
    return n;
}

std::vector<PuhegNode> discoverPuhegNodes(int timeout_ms) {
    std::map<std::string, PuhegNode> found;
    std::map<std::string, std::string> aRec;                                  // host -> ip
    std::map<std::string, std::pair<std::string, uint16_t>> srvRec;           // instance -> (host, port)

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return {};

    int on = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));

    struct sockaddr_in bindAddr{};
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    bindAddr.sin_port = htons(5353);
    if (bind(sock, (struct sockaddr*)&bindAddr, sizeof(bindAddr)) < 0) {
        bindAddr.sin_port = 0;   // авохи занят — сядем на эфемерный порт
        bind(sock, (struct sockaddr*)&bindAddr, sizeof(bindAddr));
    }

    struct ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr("224.0.0.251");
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

    struct sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = inet_addr("224.0.0.251");
    dst.sin_port = htons(5353);

    auto query = buildQuery();
    sendto(sock, query.data(), query.size(), 0,
           (struct sockaddr*)&dst, sizeof(dst));

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(sock, &rf);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        int r = select(sock + 1, &rf, nullptr, nullptr, &tv);
        if (r <= 0) continue;

        uint8_t buf[4096];
        ssize_t n = recv(sock, buf, sizeof(buf), 0);
        if (n < 12) continue;

        size_t off = 12;
        uint16_t qd = (buf[4] << 8) | buf[5];
        uint16_t total = ((buf[6] << 8) | buf[7]) + ((buf[8] << 8) | buf[9]) +
                         ((buf[10] << 8) | buf[11]);
        for (uint16_t i = 0; i < qd && off < (size_t)n; i++) {
            readName(buf, n, off);
            off += 4;
        }
        for (uint16_t i = 0; i < total && off + 10 <= (size_t)n; i++) {
            std::string name = toLower(readName(buf, n, off));
            uint16_t type = (buf[off] << 8) | buf[off + 1];
            uint16_t rdlen = (buf[off + 8] << 8) | buf[off + 9];
            off += 10;
            if (off + rdlen > (size_t)n) break;
            size_t rs = off;

            if (type == 12 && name == "_puheg._tcp.local") {
                size_t ro = rs;
                srvRec.emplace(readName(buf, n, ro), std::make_pair("", 0));
            } else if (type == 33) {                      // SRV
                uint16_t port = (buf[rs + 4] << 8) | buf[rs + 5];
                size_t ro = rs + 6;
                srvRec[readName(buf, n, ro).empty() ? "" : name] =
                    std::make_pair(readName(buf, n, ro), port);
                // перечитываем target корректно:
                size_t ro2 = rs + 6;
                srvRec[name] = std::make_pair(readName(buf, n, ro2), port);
            } else if (type == 1 && rdlen == 4) {         // A
                char ipbuf[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, buf + rs, ipbuf, sizeof(ipbuf));
                aRec[name] = ipbuf;
            }
            off = rs + rdlen;
        }
    }
    close(sock);

    for (auto& kv : srvRec) {
        PuhegNode node;
        node.instance = stripService(kv.first);
        node.port = kv.second.second;
        auto it = aRec.find(toLower(kv.second.first));
        if (it != aRec.end()) node.ip = it->second;
        found[kv.first] = node;
    }

    std::vector<PuhegNode> result;
    for (auto& kv : found) result.push_back(kv.second);
    return result;
}
