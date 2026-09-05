#ifndef EVENTEX_H
#define EVENTEX_H
#pragma once

#include <string>
#include <cstdint>
#include <vector>


struct EBMessage{
    uint16_t peer;
    std::string peerIp;

    bool binary = false;
    bool evenbus = false;
    bool words = false;

    std::string rawtext;
    std::vector<uint8_t> wordsframe;
};

class EBParser{
public:

    static void parseEmsg(std::string*);

    //обработка разных шинных сообщений
    static void wordsTopic(std::string*);
    static void processTopic(std::string*);
    static void netprofileTopic(std::string*);

private:


};

#endif // EVENTEX_H
