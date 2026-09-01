#include "commander.h"
#include "log.h"
#include <cstring>

bool Commander::isCommand(const uint8_t* data, size_t len) {
    return data != nullptr && len >= PREFIX_LEN &&
           std::memcmp(data, PREFIX, PREFIX_LEN) == 0;
}

bool Commander::isCommand(const std::string& s) {
    return s.rfind(PREFIX, 0) == 0;      // «начинается с»
}

void Commander::handle(const std::string& body, const std::string& source) {
    Log::info("Commander", "[", source, "] ", body);
    // TODO: здесь вырастет парсер команд
}
