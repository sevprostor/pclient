#include "words.h"
#include "log.h"

std::vector<uint8_t> Words::hexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    bytes.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        bytes.push_back(static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

bool Words::parseEnvelope(const std::vector<uint8_t>& data, Envelope& env) const {
    // Envelope = type (1) + sender (2) = минимум 3 байта
    if (data.size() < 3) {
        Log::warn("Words", "Слишком короткий пакет для Envelope: ", data.size(), " байт");
        return false;
    }
    env.type   = static_cast<char>(data[0]);
    env.sender = static_cast<uint16_t>((data[1] << 8) | data[2]); // network order
    return true;
}

bool Words::parseFile(const std::vector<uint8_t>& data, PwFile& file) const {
    // 1. Общий заголовок кладём прямо в file.env
    if (!parseEnvelope(data, file.env)) return false;
    if (file.env.type != 'F') {
        Log::warn("Words", "parseFile вызван для типа '", file.env.type, "'");
        return false;
    }

    size_t pos = 3; // сразу после Envelope

    // 2. Байт [proofType(2 бит) | nameSize(6 бит)]
    if (pos + 1 > data.size()) return false;
    uint8_t header = data[pos++];
    file.proofType = (header >> 6) & 0x03;   // старшие 2 бита
    uint8_t nameSize = header & 0x3F;        // младшие 6 бит

    // 3. Имя файла
    if (pos + nameSize > data.size()) {
        Log::warn("Words", "nameSize выходит за границы пакета");
        return false;
    }
    file.name.assign(reinterpret_cast<const char*>(&data[pos]), nameSize);
    pos += nameSize;

    // 4. uuid (3 байта)
    if (pos + 3 > data.size()) return false;
    file.uuid = static_cast<uint32_t>((data[pos] << 16) | (data[pos+1] << 8) | data[pos+2]);
    pos += 3;

    // 5. totalParts и part
    if (pos + 2 > data.size()) return false;
    file.totalParts = data[pos++];
    file.part       = data[pos++];

    // 6. content — всё оставшееся
    file.content.assign(data.begin() + pos, data.end());
    return true;
}

bool Words::parseProof(const std::vector<uint8_t>& data, PwProof& proof) const {
    if (!parseEnvelope(data, proof.env)) return false;
    // TODO: поля proof
    return true;
}

bool Words::parseCommand(const std::vector<uint8_t>& data, PwCommand& cmd) const {
    if (!parseEnvelope(data, cmd.env)) return false;
    // TODO: поля command
    return true;
}
