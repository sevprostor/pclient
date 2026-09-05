#include "file.h"
#include "config.h"
#include "log.h"
#include <fstream>
#include <algorithm>
#include <random>

File::File() {
    // Конструктор может инициализировать базовые пути, если нужно
}

fs::path File::getInboxDir(uint16_t senderId) const {
    // <workDir>/<myIp>/inbox/<senderId>/
    fs::path base = config.workDir;
    base /= config.myContact.ip.empty() ? "unknown" : config.myContact.ip;
    base /= "inbox";
    base /= std::to_string(senderId);
    return base;
}

fs::path File::getOutboxDir(const std::string& contactIp) const {
    // <workDir>/<myIp>/outbox/<contactIp>/
    fs::path base = config.workDir;
    base /= config.myContact.ip.empty() ? "unknown" : config.myContact.ip;
    base /= "outbox";
    base /= contactIp;
    return base;
}

fs::path File::getOutgoingDir(const std::string& contactIp, const std::string& filename) const {
    // <workDir>/<myIp>/outbox/<contactIp>/outgoing/<filename>/
    return getOutboxDir(contactIp) / "outgoing" / filename;
}

bool File::ensureDir(const fs::path& dir) const {
    std::error_code ec;
    if (fs::exists(dir, ec)) {
        return fs::is_directory(dir, ec);
    }
    fs::create_directories(dir, ec);
    if (ec) {
        Log::error("File", "❌ Не удалось создать каталог ", dir.string(), ": ", ec.message());
        return false;
    }
    Log::info("File", "📁 Создан каталог: ", dir.string());
    return true;
}

bool File::assembleFile(const PwFile& pwFile) {
    uint32_t uuid = pwFile.uuid;
    int part = pwFile.part;
    int totalParts = pwFile.totalParts;

    // Определяем путь к inbox для этого отправителя
    fs::path inboxDir = getInboxDir(pwFile.env.sender);
    if (!ensureDir(inboxDir)) {
        Log::error("File", "❌ Не удалось создать inbox для sender=", pwFile.env.sender);
        return false;
    }

    // Путь к подкаталогу uuid: inbox/<senderId>/<uuid>/
    fs::path uuidDir = inboxDir / std::to_string(uuid);
    if (!ensureDir(uuidDir)) {
        Log::error("File", "❌ Не удалось создать каталог для uuid=", uuid);
        return false;
    }

    // Сохраняем чанок в файл: <part>-<totalParts>-<filename>
    std::string chunkName = std::to_string(part) + "-" + std::to_string(totalParts) + "-" + pwFile.name;
    fs::path chunkPath = uuidDir / chunkName;

    std::ofstream out(chunkPath.string(), std::ios::binary | std::ios::trunc);
    if (!out) {
        Log::error("File", "❌ Не удалось сохранить чанок: ", chunkPath.string());
        return false;
    }
    out.write(reinterpret_cast<const char*>(pwFile.content.data()),
              static_cast<std::streamsize>(pwFile.content.size()));
    out.close();

    Log::info("File", "📥 Часть ", part + 1, "/", totalParts, " файла ", pwFile.name,
              " (uuid=", uuid, ") сохранена");

    // Сканируем каталог uuid/ и проверяем, все ли части на месте
    std::error_code ec;
    if (!fs::exists(uuidDir, ec) || !fs::is_directory(uuidDir, ec)) {
        return false;
    }

    // Собираем найденные части: part -> путь к файлу
    std::map<int, fs::path> foundParts;
    int foundTotalParts = 0;
    std::string foundFilename;

    for (const auto& entry : fs::directory_iterator(uuidDir, ec)) {
        if (!entry.is_regular_file()) continue;

        std::string fileName = entry.path().filename().string();

        // Парсим имя: <part>-<totalParts>-<filename>
        size_t firstDash = fileName.find('-');
        if (firstDash == std::string::npos) continue;

        size_t secondDash = fileName.find('-', firstDash + 1);
        if (secondDash == std::string::npos) continue;

        try {
            int p = std::stoi(fileName.substr(0, firstDash));
            int tp = std::stoi(fileName.substr(firstDash + 1, secondDash - firstDash - 1));
            std::string fname = fileName.substr(secondDash + 1);

            foundParts[p] = entry.path();
            foundTotalParts = tp;
            foundFilename = fname;
        } catch (...) {
            Log::warn("File", "⚠️ Неправильное имя чанка: ", fileName);
        }
    }

    // Проверяем, все ли части получены
    if (foundTotalParts == 0 || static_cast<int>(foundParts.size()) < foundTotalParts) {
        // Ещё не все части на месте
        return false;
    }

    // Проверяем, что все части от 0 до totalParts-1 присутствуют
    for (int i = 0; i < foundTotalParts; ++i) {
        if (foundParts.find(i) == foundParts.end()) {
            Log::error("File", "❌ Отсутствует часть ", i, " файла ", foundFilename);
            return false;
        }
    }

    // Все части на месте — собираем файл из файлов на диске
    std::vector<uint8_t> completeData;

    for (int i = 0; i < foundTotalParts; ++i) {
        fs::path chunkFile = foundParts[i];

        std::ifstream chunkIn(chunkFile.string(), std::ios::binary | std::ios::ate);
        if (!chunkIn) {
            Log::error("File", "❌ Не удалось открыть чанк: ", chunkFile.string());
            return false;
        }

        std::streamsize chunkSize = chunkIn.tellg();
        chunkIn.seekg(0, std::ios::beg);

        std::vector<char> chunkData(chunkSize);
        if (!chunkIn.read(chunkData.data(), chunkSize)) {
            Log::error("File", "❌ Ошибка чтения чанка: ", chunkFile.string());
            return false;
        }
        chunkIn.close();

        completeData.insert(completeData.end(), chunkData.begin(), chunkData.end());
    }

    // Сохраняем финальный файл в inbox/<senderId>/
    fs::path finalPath = inboxDir / foundFilename;
    std::ofstream finalOut(finalPath.string(), std::ios::binary | std::ios::trunc);
    if (!finalOut) {
        Log::error("File", "❌ Не удалось открыть для записи: ", finalPath.string());
        return false;
    }
    finalOut.write(reinterpret_cast<const char*>(completeData.data()),
                   static_cast<std::streamsize>(completeData.size()));
    finalOut.close();

    Log::info("File", "✅ Файл ", foundFilename, " полностью собран и сохранён в ",
              finalPath.string(), " (", completeData.size(), " байт)");

    // Удаляем временный каталог с чанками
    fs::remove_all(uuidDir, ec);
    if (ec) {
        Log::warn("File", "⚠️ Не удалось удалить временный каталог: ", uuidDir.string());
    } else {
        Log::info("File", "✓ Временный каталог удалён: ", uuidDir.string());
    }

    return true;
}

bool File::isComplete(uint32_t uuid) const {
    auto it = parts_.find(uuid);
    if (it == parts_.end()) return false;

    const auto& partMap = it->second;
    if (partMap.empty()) return false;

    // Проверяем, есть ли все части от 0 до totalParts-1
    int expectedTotal = partMap.begin()->second.totalParts;
    if (partMap.size() != static_cast<size_t>(expectedTotal)) return false;

    for (int i = 0; i < expectedTotal; ++i) {
        if (partMap.find(i) == partMap.end()) return false;
    }
    return true;
}

std::string File::getCompletePath(uint32_t uuid) const {
    // Этот метод используется после assembleFile, когда файл уже сохранён
    // Возвращает пустую строку, если файл не найден
    return "";
}

//////////////////////////////////////////////////////////////////////////////////
fs::path File::getOutgoingDir(const std::string& contactIp, uint32_t uuid) const {
    // <workDir>/<myIp>/outbox/<contactIp>/outgoing/<uuid>/
    return getOutboxDir(contactIp) / "outgoing" / std::to_string(uuid);
}
//////////////////////////////////////////////////////////////////////////////////

// ============================================================
// Время модификации файла -> epoch-секунды (для сравнения старшинства)
// ============================================================
static uint64_t fileTimeToEpoch(const fs::path& p) {
    std::error_code ec;
    auto ftime = fs::last_write_time(p, ec);
    if (ec) return 0;

    // C++17: пересчёт file_time_type в system_clock через «сейчас»
    auto nowFs = fs::file_time_type::clock::now();
    auto nowSys = std::chrono::system_clock::now();
    auto tp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - nowFs + nowSys);

    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count());
}

fs::path File::getSpoolDir(const std::string& destIp, uint32_t uuid) const {
    // <workDir>/<myIp>/outbox/spool/<destIp>-<uuid>/
    fs::path base = config.workDir;
    base /= config.myContact.ip.empty() ? "unknown" : config.myContact.ip;
    base /= "outbox";
    base /= "spool";
    base /= destIp + "-" + std::to_string(uuid);
    return base;
}

// ============================================================
// Обход каталогов контактов: по 1 самому старому файлу из каждого.
// Несуществующие каталоги создаются.
// ============================================================
std::vector<File::OutboxFile> File::scanOutboxAll() {
    std::vector<OutboxFile> result;

    for (const auto& contact : config.addressbook) {
        const std::string& ip = contact.ip;
        fs::path dir = getOutboxDir(ip);

        // Каталога нет — создаём и идём дальше (файлов в нём пока нет)
        if (!ensureDir(dir)) continue;

        // Ищем самый старый обычный файл в корне каталога
        std::error_code ec;
        bool found = false;
        OutboxFile oldest;

        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file(ec)) continue; // spool/ и пр. каталоги не трогаем

            OutboxFile of;
            of.fullPath = entry.path();
            of.filename = entry.path().filename().string();
            of.destIp = ip;
            of.timeCreated = fileTimeToEpoch(entry.path());
            of.size = entry.file_size(ec);

            if (!found || of.timeCreated < oldest.timeCreated) {
                oldest = of;
                found = true;
            }
        }

        if (found) result.push_back(oldest);
    }

    return result;
}

// ============================================================
// Нарезка самого старого файла в spool/<destIp>-<uuid>/,
// исходный файл удаляется. Отправки пока нет.
// ============================================================
uint32_t File::spoolFile(const OutboxFile& of, int chunkSize) {
    // Читаем файл целиком
    std::ifstream in(of.fullPath.string(), std::ios::binary | std::ios::ate);
    if (!in) {
        Log::error("File", "❌ Не удалось открыть файл: ", of.fullPath.string());
        return 0;
    }
    std::streamsize fileSize = in.tellg();
    in.seekg(0, std::ios::beg);

    std::vector<char> buffer(fileSize);
    if (fileSize > 0 && !in.read(buffer.data(), fileSize)) {
        Log::error("File", "❌ Ошибка чтения файла: ", of.fullPath.string());
        return 0;
    }
    in.close();

    // Генерируем uuid передачи
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(1, 0xFFFFFF);
    uint32_t uuid = dist(gen);

    // Каталог spool/<destIp>-<uuid>/
    fs::path spoolDir = getSpoolDir(of.destIp, uuid);
    if (!ensureDir(spoolDir)) return 0;

    // Количество чанков (минимум 1, даже для пустого файла)
    int totalParts = static_cast<int>((fileSize + chunkSize - 1) / chunkSize);
    if (totalParts == 0) totalParts = 1;

    Log::info("File", "📦 Нарезка ", of.filename, " (", fileSize, " байт) на ",
              totalParts, " чанков -> ", spoolDir.string());

    // Сохраняем чанки: <part>-<totalParts>-<filename>
    for (int part = 0; part < totalParts; ++part) {
        std::streamsize start = static_cast<std::streamsize>(part) * chunkSize;
        std::streamsize end = std::min(start + chunkSize, fileSize);
        std::streamsize size = end - start; // может быть 0 для пустого файла

        std::string chunkName = std::to_string(part) + "-" +
                                std::to_string(totalParts) + "-" + of.filename;
        fs::path chunkPath = spoolDir / chunkName;

        std::ofstream out(chunkPath.string(), std::ios::binary | std::ios::trunc);
        if (!out) {
            Log::error("File", "❌ Не удалось создать чанк: ", chunkPath.string());
            return 0;
        }
        if (size > 0) out.write(buffer.data() + start, size);
        out.close();
    }

    // Удаляем исходный файл из outbox/<destIp>/
    std::error_code ec;
    fs::remove(of.fullPath, ec);
    if (ec) {
        Log::warn("File", "⚠️ Не удалось удалить исходный файл: ", of.fullPath.string());
    } else {
        Log::info("File", "✓ Исходный файл удалён: ", of.fullPath.string());
    }

    Log::info("File", "✅ Файл ", of.filename, " в spool (uuid=", uuid, ")");
    return uuid;
}

