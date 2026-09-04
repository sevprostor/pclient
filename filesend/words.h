#pragma once
#include <cstdint>
#include <string>
#include <vector>

// ===========================================================================
// СТРУКТУРЫ — только данные, без логики
// ===========================================================================

// Общий заголовок любого PW-пакета (после магика PW\x01):
//   type   (1 байт) — '0' proof | 'F' file | 'C' command
//   sender (2 байта) — последние октеты IP = MAC
struct Envelope {
    char     type   = 0;
    uint16_t sender = 0;
};

// Пакет типа 'F' — чанк файла:
//   [proofType(2 бит) | nameSize(6 бит)] (1 байт)
//   name (nameSize) | uuid (3) | totalParts (1) | part (1) | content (...)
struct PwFile {
    Envelope   env;
    uint8_t    proofType  = 0;
    std::string name;
    uint32_t   uuid       = 0;
    uint8_t    totalParts = 0;
    uint8_t    part       = 0;
    std::vector<uint8_t> content;
};

struct PwProof   { Envelope env; /* TODO */ };
struct PwCommand { Envelope env; /* TODO */ };

// ===========================================================================
// КЛАСС — методы разбора; заполняют структуры по ссылке
// ===========================================================================

class Words {
public:
    // Разбор общего заголовка: type (1) + sender (2)
    bool parseEnvelope(const std::vector<uint8_t>& data, Envelope& env) const;

    // Разбор кадра типа 'F' (включая envelope -> file.env)
    bool parseFile(const std::vector<uint8_t>& data, PwFile& file) const;

    // НОВОЕ: собрать кадр типа 'F' для отправки
    static std::vector<uint8_t> buildFileFrame(
        uint16_t sender,
        uint32_t uuid,
        const std::string& name,
        uint8_t totalParts,
        uint8_t part,
        const std::vector<uint8_t>& content,
        uint8_t proofType = 0
        );

    // Заглушки на будущее
    bool parseProof(const std::vector<uint8_t>& data, PwProof& proof) const;
    bool parseCommand(const std::vector<uint8_t>& data, PwCommand& cmd) const;

    // Утилита: hex-строка -> байты
    static std::vector<uint8_t> hexToBytes(const std::string& hex);
};
