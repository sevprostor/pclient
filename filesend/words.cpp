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

std::vector<uint8_t> Words::buildFileFrame(
    uint16_t sender,
    uint32_t uuid,
    const std::string& name,
    uint8_t totalParts,
    uint8_t part,
    const std::vector<uint8_t>& content,
    uint8_t proofType)
{
    std::vector<uint8_t> frame;
    std::string name_b = name;

    if (name_b.size() > 63) {
        Log::error("Words", "Имя файла длиннее 63 байт: ", name);
        name_b = name_b.substr(0, 63);
    }

    uint8_t header = ((proofType & 0x3) << 6) | (name_b.size() & 0x3F);

    // type
    frame.push_back('F');

    // sender (network order)
    frame.push_back((sender >> 8) & 0xFF);
    frame.push_back(sender & 0xFF);

    // header
    frame.push_back(header);

    // name
    frame.insert(frame.end(), name_b.begin(), name_b.end());

    // uuid (24 бита)
    frame.push_back((uuid >> 16) & 0xFF);
    frame.push_back((uuid >> 8) & 0xFF);
    frame.push_back(uuid & 0xFF);

    // totalParts, part
    frame.push_back(totalParts);
    frame.push_back(part);

    // content
    frame.insert(frame.end(), content.begin(), content.end());

    return frame;
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
