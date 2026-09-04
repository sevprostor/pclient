#include "file.h"
#include "log.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace fs = std::filesystem;

bool File::assembleFile(const PwFile& pwFile) {
    const std::string inboxDir = "./inbox";
    const std::string uuidStr = std::to_string(pwFile.uuid);
    const std::string tempDir = inboxDir + "/" + uuidStr;
    const std::string filename = pwFile.name;

    // Создаём inbox если его нет
    if (!fs::exists(inboxDir)) {
        fs::create_directory(inboxDir);
        Log::info("File", "Создана директория: ", inboxDir);
    }

    // Создаём временный каталог для этого uuid
    if (!fs::exists(tempDir)) {
        fs::create_directory(tempDir);
        Log::info("File", "Создана временная директория: ", tempDir);
    }

    // Формируем имя файла чанка: <part>-<totalParts>-<filename>
    std::ostringstream chunkName;
    chunkName << (int)pwFile.part << "-" << (int)pwFile.totalParts << "-" << filename;
    const std::string chunkPath = tempDir + "/" + chunkName.str();

    // Записываем чанк на диск
    std::ofstream ofs(chunkPath, std::ios::binary);
    if (!ofs) {
        Log::error("File", "Не удалось открыть для записи: ", chunkPath);
        return false;
    }
    ofs.write(reinterpret_cast<const char*>(pwFile.content.data()), pwFile.content.size());
    ofs.close();
    Log::info("File", "Сохранён чанк: ", chunkPath, " (", pwFile.content.size(), " байт)");

    // Проверяем, все ли чанки получены
    if (allChunksReceived(inboxDir, pwFile.uuid, pwFile.totalParts, filename)) {
        Log::info("File", "Все чанки получены, собираю файл...");
        return finalizeFile(inboxDir, pwFile.uuid, pwFile.totalParts, filename);
    }

    return false;
}

bool File::allChunksReceived(const std::string& inboxDir, uint32_t uuid,
                             uint8_t totalParts, const std::string& filename) {
    const std::string tempDir = inboxDir + "/" + std::to_string(uuid);

    // Проверяем наличие всех файлов от 0 до totalParts-1
    for (uint8_t part = 0; part < totalParts; ++part) {
        std::ostringstream chunkName;
        chunkName << (int)part << "-" << (int)totalParts << "-" << filename;
        const std::string chunkPath = tempDir + "/" + chunkName.str();

        if (!fs::exists(chunkPath)) {
            return false;  // хотя бы один чанк отсутствует
        }
    }
    return true;
}

bool File::finalizeFile(const std::string& inboxDir, uint32_t uuid,
                        uint8_t totalParts, const std::string& filename) {
    const std::string tempDir = inboxDir + "/" + std::to_string(uuid);
    const std::string finalPath = inboxDir + "/" + filename;

    // Открываем итоговый файл для записи
    std::ofstream ofs(finalPath, std::ios::binary);
    if (!ofs) {
        Log::error("File", "Не удалось открыть итоговый файл: ", finalPath);
        return false;
    }

    // Склеиваем чанки по порядку
    for (uint8_t part = 0; part < totalParts; ++part) {
        std::ostringstream chunkName;
        chunkName << (int)part << "-" << (int)totalParts << "-" << filename;
        const std::string chunkPath = tempDir + "/" + chunkName.str();

        std::ifstream ifs(chunkPath, std::ios::binary);
        if (!ifs) {
            Log::error("File", "Не удалось открыть чанк: ", chunkPath);
            return false;
        }

        // Копируем содержимое чанка в итоговый файл
        ofs << ifs.rdbuf();
        ifs.close();
    }
    ofs.close();

    Log::info("File", "✅ Файл собран: ", finalPath);

    // Удаляем временный каталог со всеми чанками
    fs::remove_all(tempDir);
    Log::info("File", "Удалена временная директория: ", tempDir);

    return true;
}
