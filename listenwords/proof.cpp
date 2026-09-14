#include "proof.h"
#include "words.h"
#include "file.h"
#include "config.h"
#include "log.h"
#include <filesystem>
#include <fstream>
namespace fs = std::filesystem;

//bool Proof::parseProof(const std::vector<uint8_t>& data, PwProof& proof) const {

//    Words words;

//    if (!words.parseEnvelope(data, proof.env)) return false;
    // TODO: поля proof

    // Это все происходит в рамках процесса ОТПРАВКИ
    //
    // Proof - это точно такой же F-пакет. В нем указано uuid идущей передачи,
    // а в имени файла - "PF". Содержимое файла - байты с номерами полученных частей.
    // Если в outbox/<senderIp>/<uuid>/waiting/ есть части, упомянутые в пруфе,
    // то перенести их в outbox/<senderIp>/<uuid>/approved/


//    return true;
//}
/*
bool Proof::sendSingleProof(uint16_t senderMac, uint32_t uuid, int part) {
    Log::info("Proof", "📤 Отправка пруфа для одного чанка: uuid=", uuid, ", part=", part);

    // Находим IP отправителя
    std::string senderIp = "unknown";
    for (const auto& contact : config.addressbook) {
        if (contact.mac == senderMac) {
            senderIp = contact.ip;
            break;
        }
    }

    if (senderIp == "unknown") {
        Log::error("Proof", "❌ Отправитель не найден в адресной книге");
        return false;
    }

    // Путь к каталогу outbox/<senderIp>/
    fs::path outboxDir = fs::path(config.workDir) /
                         (config.myContact.ip.empty() ? "unknown" : config.myContact.ip) /
                         "outbox" / senderIp;

    // Создаём каталог если не существует
    std::error_code ec;
    if (!fs::exists(outboxDir, ec)) {
        fs::create_directories(outboxDir, ec);
        if (ec) {
            Log::error("Proof", "❌ Не удалось создать каталог outbox/", senderIp);
            return false;
        }
    }

    // Путь к файлу пруфа: outbox/<senderIp>/<uuid>.pwProof
    fs::path proofPath = outboxDir / (std::to_string(uuid) + ".pwproof");

    // Содержимое: один байт с номером части
    std::vector<uint8_t> content;
    content.push_back(static_cast<uint8_t>(part));

    content.push_back('X');

    // Записываем файл
    std::ofstream out(proofPath.string(), std::ios::binary | std::ios::trunc);
    if (!out) {
        Log::error("Proof", "❌ Не удалось создать файл пруфа: ", proofPath.string());
        return false;
    }
    out.write(reinterpret_cast<const char*>(content.data()), content.size());
    out.close();

    Log::info("Proof", "✓ Файл пруфа создан: ", proofPath.string(),
              " (uuid=", uuid, ", part=", part, ")");
    return true;
}
*/

bool Proof::sendFileProof(uint16_t senderMac, uint32_t uuid, std::vector<int>& parts) {
    Log::info("Proof", "📤 Отправка пруфа для uuid=", uuid);

    // Находим IP отправителя
    std::string senderIp = "unknown";
    for (const auto& contact : config.addressbook) {
        if (contact.mac == senderMac) {
            senderIp = contact.ip;
            break;
        }
    }

    if (senderIp == "unknown") {
        Log::error("Proof", "❌ Отправитель не найден в адресной книге");
        return false;
    }

    // Путь к каталогу outbox/<senderIp>/
    fs::path outboxDir = fs::path(config.workDir) /
                         (config.myContact.ip.empty() ? "unknown" : config.myContact.ip) /
                         "outbox" / senderIp;

    // Создаём каталог если не существует
    std::error_code ec;
    if (!fs::exists(outboxDir, ec)) {
        fs::create_directories(outboxDir, ec);
        if (ec) {
            Log::error("Proof", "❌ Не удалось создать каталог outbox/", senderIp);
            return false;
        }
    }

    // Путь к файлу пруфа: outbox/<senderIp>/<uuid>.pwProof
    fs::path proofPath = outboxDir / (std::to_string(uuid) + ".pwproof");

    // Содержимое: один байт с номером части
    std::vector<uint8_t> content;
    std::string partsForLog;

    for(int i = 0; i < parts.size(); i++){
        content.push_back(static_cast<uint8_t>(parts[i]));
        partsForLog += std::to_string(static_cast<uint8_t>(parts[i])) + ",";
    }

    // Записываем файл
    std::ofstream out(proofPath.string(), std::ios::binary | std::ios::trunc);
    if (!out) {
        Log::error("Proof", "❌ Не удалось создать файл пруфа: ", proofPath.string());
        return false;
    }
    out.write(reinterpret_cast<const char*>(content.data()), content.size());
    out.close();

    Log::info("Proof", "✓ Файл пруфа создан: ", proofPath.string(),
              " (uuid=", uuid, ", parts=", partsForLog, ")");
    return true;
}

/*
bool Proof::sendBatchProof(uint16_t senderMac, uint32_t uuid, const std::vector<int>& parts) {
    Log::info("Proof", "📤 Отправка общего пруфа: uuid=", uuid,
              ", чанков: ", parts.size());

    // Находим IP отправителя
    std::string senderIp = "unknown";
    for (const auto& contact : config.addressbook) {
        if (contact.mac == senderMac) {
            senderIp = contact.ip;
            break;
        }
    }

    if (senderIp == "unknown") {
        Log::error("Proof", "❌ Отправитель не найден в адресной книге");
        return false;
    }

    fs::path spoolBase = fs::path(config.workDir) /
                         (config.myContact.ip.empty() ? "unknown" : config.myContact.ip) /
                         "outbox" / "spool";

    std::string pwpName = "0-1-0-" + senderIp + "-" + std::to_string(uuid) + "-batch.pwp";
    fs::path pwpPath = spoolBase / pwpName;

    std::ofstream out(pwpPath.string(), std::ios::binary);
    if (!out) {
        Log::error("Proof", "❌ Не удалось создать .pwp для batch-пруфа");
        return false;
    }

    // TODO: записать реальные данные batch-пруфа (массив номеров частей)
    out.close();

    Log::info("Proof", "✓ Batch-пруф создан: ", pwpPath.string());
    return true;
}*/
