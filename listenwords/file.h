#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include "words.h"
#include <filesystem>

namespace fs = std::filesystem;



class File {
public:
    File();

    struct OutboxFile {
        fs::path fullPath;
        std::string filename;
        std::string destIp;
        uint64_t timeCreated;
        size_t size;
    };

    // Сборка файла из чанков (существующий метод)
    bool assembleFile(const PwFile& pwFile);

    // НОВОЕ: проверить, завершён ли файл с данным uuid
    bool isComplete(uint32_t uuid) const;

    // НОВОЕ: получить путь к завершённому файлу
    std::string getCompletePath(uint32_t uuid) const;

    // НОВОЕ: сканировать outbox/<contactIp>/ и вернуть список файлов для отправки

    //std::vector<OutboxFile> scanOutbox(const std::string& contactIp);


    struct Chunk {
        fs::path path;
        int part;
        int totalParts;
        std::string filename;
        uint32_t uuid;
    };


    // НОВОЕ: по 1 самому старому файлу из каждого каталога контакта;
    // несуществующие каталоги создаются
    std::vector<OutboxFile> scanOutboxAll();

    // НОВОЕ: нарезать файл на чанки в outbox/spool/<destIp>-<uuid>/,
    // исходный файл удалить. Возвращает uuid (0 = ошибка)
    uint32_t spoolFile(const OutboxFile& of, int chunkSize = 2000);

private:
    // <workDir>/<myIp>/outbox/spool/<destIp>-<uuid>/
    fs::path getSpoolDir(const std::string& destIp, uint32_t uuid) const;


    // Путь к каталогу чанков: <workDir>/<myIp>/outbox/<contactIp>/outgoing/<uuid>/
    fs::path getOutgoingDir(const std::string& contactIp, uint32_t uuid) const;

    // Путь к директории входящих файлов: <workDir>/<myIp>/inbox/<senderIp>/
    fs::path getInboxDir(uint16_t senderId) const;

    // Путь к директории исходящих файлов: <workDir>/<myIp>/outbox/<contactIp>/
    fs::path getOutboxDir(const std::string& contactIp) const;

    // Путь к директории чанков: <workDir>/<myIp>/outbox/<contactIp>/outgoing/<filename>/
    fs::path getOutgoingDir(const std::string& contactIp, const std::string& filename) const;

    // Создание каталога, если не существует
    bool ensureDir(const fs::path& dir) const;

    // Хранилище частей файлов: uuid -> map<part, PwFile>
    std::map<uint32_t, std::map<int, PwFile>> parts_;
};
