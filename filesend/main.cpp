#include "config.h"
#include "transport.h"
#include "words.h"
#include "log.h"
#include "../libs/json.hpp"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <random>
#include <filesystem>

using json = nlohmann::json;
//Config config;

static void usage(const char* prog) {
    Log::info("FileSend", "Использование: ", prog, " <файл> <dest_ip> [опции]");
    Log::info("FileSend", "Опции:");
    Log::info("FileSend", "  -chunk <размер>   Размер чанка в байтах (по умолчанию 2000)");
    Log::info("FileSend", "  -delay <мс>       Задержка между чанками в мс (по умолчанию 500)");
    Log::info("FileSend", "  -eb <порт>        Порт EventBus драйвера (по умолчанию 9400)");
    Log::info("FileSend", "  -sender <IP>      IP отправителя (по умолчанию 10.0.6.84)");
}

static uint16_t ipLastOctets(const std::string& ip) {
    // Извлекаем последние два октета IP для sender
    size_t last_dot = ip.rfind('.');
    if (last_dot == std::string::npos) return 0;
    size_t prev_dot = ip.rfind('.', last_dot - 1);
    if (prev_dot == std::string::npos) return 0;

    uint8_t octet3 = std::stoi(ip.substr(prev_dot + 1, last_dot - prev_dot - 1));
    uint8_t octet4 = std::stoi(ip.substr(last_dot + 1));
    return (static_cast<uint16_t>(octet3) << 8) | octet4;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        usage(argv[0]);
        return 1;
    }

    // Парсим аргументы командной строки
    std::string filePath = argv[1];
    std::string destIp = argv[2];
    std::string senderIp = "10.0.6.84";
    int chunkSize = 2000;
    int delayMs = 500;
    uint16_t ebPort = 9400;

    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-chunk" && i + 1 < argc) {
            chunkSize = std::stoi(argv[++i]);
        } else if (arg == "-delay" && i + 1 < argc) {
            delayMs = std::stoi(argv[++i]);
        } else if (arg == "-eb" && i + 1 < argc) {
            ebPort = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "-sender" && i + 1 < argc) {
            senderIp = argv[++i];
        }
    }

    // Читаем файл
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file) {
        Log::error("FileSend", "Не удалось открыть файл: ", filePath);
        return 1;
    }

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(fileSize);
    if (!file.read(buffer.data(), fileSize)) {
        Log::error("FileSend", "Ошибка чтения файла");
        return 1;
    }
    file.close();

    std::string filename = std::filesystem::path(filePath).filename().string();
    Log::info("FileSend", "Файл: ", filename, " (", fileSize, " байт)");

    // Режем на чанки
    std::vector<std::vector<uint8_t>> chunks;
    for (std::streamsize i = 0; i < fileSize; i += chunkSize) {
        std::streamsize end = std::min(static_cast<std::streamsize>(i + chunkSize), fileSize);
        chunks.emplace_back(buffer.begin() + i, buffer.begin() + end);
    }

    uint8_t totalParts = static_cast<uint8_t>(chunks.size());
    Log::info("FileSend", "Разбито на ", static_cast<int>(totalParts), " чанков по ", chunkSize, " байт");

    // Генерируем UUID и sender
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(1, 0xFFFFFF);
    uint32_t uuid = dist(gen);
    uint16_t sender = ipLastOctets(senderIp);

    Log::info("FileSend", "UUID: ", uuid, ", sender: ", sender);

    // Инициализируем Transport
    Transport transport;
    if (!transport.init(ebPort)) {
        Log::error("FileSend", "Не удалось инициализировать Transport");
        return 1;
    }

    // Подписываемся на события (для мониторинга)
    transport.setOnEvent([](const std::string& text) {
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded()) return;

        // НОВОЕ: Обработка netprofile
        if (j.contains("netprofile")) {
            const auto& np = j["netprofile"];
            Log::info("FileSend", "📡 NetProfile получен:");
            Log::info("FileSend", "  puheg_ip: ", np.value("puheg_ip", "unknown"));

            if (np.contains("contacts") && np["contacts"].is_array()) {
                Log::info("FileSend", "  contacts (", np["contacts"].size(), "):");

                config.addressbook.resize(0);

                Config::abc contact;
                std::vector<Config::abc> abook;

                for (const auto& c : np["contacts"]) {

                    Log::info("FileSend", "    - ", c.value("ip", "?"),
                              " (id=", c.value("id", 0), ", name=", c.value("name", "?"), ")");

                    contact.ip = c.value("ip", "");
                    contact.key = c.value("key", "");
                    contact.mac = c.value("id", 0);

                    if(c.contains("myOwn")){
                        //Мой контакт
                        config.myContact = contact;
                        continue;
                    }

                    config.addressbook.emplace_back(contact);

                }
            }
            return;
        }

        // Логируем важные события
        if (j.contains("process")) {
            Log::info("FileSend", "📋 Process: ", j["process"].dump());
        } else if (j.contains("transport")) {
            Log::info("FileSend", "🚚 Transport: ", j["transport"].dump());
        }
    });

    // Добавляем магик PW\x01 перед каждым кадром
    std::vector<uint8_t> magic = {'P', 'W', 0x01};

    Log::info("FileSend", "Начинаю отправку на ", destIp, "...");

    // Отправляем чанки
    for (uint8_t part = 0; part < totalParts; ++part) {
        std::vector<uint8_t> frame = Words::buildFileFrame(
            sender, uuid, filename, totalParts, part, chunks[part]
            );

        std::vector<uint8_t> payload;
        payload.reserve(magic.size() + frame.size());
        payload.insert(payload.end(), magic.begin(), magic.end());
        payload.insert(payload.end(), frame.begin(), frame.end());

        if (!transport.sendFrame(destIp, payload)) {
            Log::error("FileSend", "Ошибка отправки чанка ", part);
            return 1;
        }

        Log::info("FileSend", "Отправлен чанок ", part + 1, "/", totalParts,
                  " (", chunks[part].size(), " байт)");

        // Задержка между чанками
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));

        // Обрабатываем входящие события (toss_ok, radio_busy и т.д.)
        transport.poll(10);
    }

    Log::info("FileSend", "✅ Файл отправлен полностью");
    transport.stop();
    return 0;
}
