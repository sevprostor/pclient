#ifndef FILE_H
#define FILE_H
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "words.h"

// Класс для сборки файлов из чанков PW-протокола.
// Каждый чанк сохраняется во временную директорию ./inbox/<uuid>/
// После получения всех чанков файл склеивается в ./inbox/<filename>
class File {
public:
    // Сборка файла из чанка.
    // Возвращает true, если файл полностью собран и перемещён в ./inbox/
    bool assembleFile(const PwFile& pwFile);

private:
    // Проверяет, все ли чанки получены для данного uuid
    bool allChunksReceived(const std::string& inboxDir, uint32_t uuid,
                           uint8_t totalParts, const std::string& filename);

    // Склеивает все чанки в один файл и удаляет временный каталог
    bool finalizeFile(const std::string& inboxDir, uint32_t uuid,
                      uint8_t totalParts, const std::string& filename);
};
#endif // FILE_H
