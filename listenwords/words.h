#ifndef WORDS_H
#define WORDS_H
#pragma once
//#include "transport.h"
#include <string>
#include <atomic>
#include <vector>

/*

Пакет: PW(3 байта) заведомо отброшены
Envelope
type (0) | sender (1-2) | payload (3....) |

types
'0' - proof
'F' - file
'C' - command

sender - последние октеты IP (мак-адрес)
uuid - уникальный номер передачи

PWfile (F)
[proofType (2bit)| nameSize (6bit)] (total 1 byte) | name (nameSize) |  uuid (3) | totalParts (1) | part (1) |

*/

struct Envelope{
    uint8_t type;
};

struct Pwfile{
    uint32_t sender;
    uint32_t id;
    std::string fname;
    uint8_t totalParts;
    uint8_t thisPart;
    std::vector<uint8_t> content;
};

class PuhegWords {
public:

    Pwfile recieveFile();
    //PuhegWords(Transport& transport);

    //bool sendFile(const std::string& filePath, const std::string& destIp);
    //void recvFiles(const std::string& outDir);
    //void stop();

private:
    //Transport& transport_;
    //std::atomic<bool> running_{false};
};
#endif // HAULER_H
