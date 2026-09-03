#include "words.h"
#include <cstdio>
#include <fstream>
#include <thread>
#include <chrono>

PuhegWords::PuhegWords(Transport& transport) : transport_(transport) {}

bool PuhegWords::sendFile(const std::string& filePath, const std::string& destIp) {
    std::ifstream f(filePath, std::ios::binary);
    if (!f) {
        fprintf(stderr, "Не открыть %s\n", filePath.c_str());
        return false;
    }
    f.seekg(0, std::ios::end);
    size_t size = (size_t)f.tellg();
    printf("Отправка %s (%zu байт) -> %s\n", filePath.c_str(), size, destIp.c_str());

    // ШАГ 2: Тестовый FH-фрейм
    std::vector<uint8_t> testFrame = {'P', 'W', '\x01', 'H', 'E', 'L', 'L', 'O'};

    printf("Отправка бинарного FH-фрейма (%zu байт) на IP %s\n", testFrame.size(), destIp.c_str());
    if (!transport_.sendFrame(destIp, testFrame)) {
        fprintf(stderr, "Ошибка отправки фрейма\n");
        return false;
    }

    printf("Фрейм передан в драйвер. Ожидаем ответ 5 сек...\n");
    transport_.setOnFrame([](const std::vector<uint8_t>& frame) {
        if (frame.size() >= 3 && frame[0] == 'F' && frame[1] == 'H' && frame[2] == '\x01') {
            printf("✅ Получен ответный FH-фрейм от драйвера! (%zu байт)\n", frame.size());
        }
    });

    running_ = true;
    for (int i = 0; i < 50 && running_; ++i) {
        transport_.poll(100);
    }

    return true;
}

void PuhegWords::recvFiles(const std::string& outDir) {
    printf("Приём файлов в директорию: %s\n", outDir.c_str());
    running_ = true;

    //сперва подписаться в драйвере на транспортные сообщения


    transport_.setOnFrame([](const std::vector<uint8_t>& frame) {
        if (frame.size() >= 3 && frame[0] == 'F' && frame[1] == 'H' && frame[2] == '\x01') {
            printf("📥 Получен FH-фрейм (%zu байт)\n", frame.size());
        }
    });

    while (running_) {
        transport_.poll(100);
    }
}

void PuhegWords::stop() {
    running_ = false;
}
