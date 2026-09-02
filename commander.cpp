#include "commander.h"
#include "log.h"
#include "addressbook.h"
#include <cstring>
#include "TCPclient.h"
#include "msgparser.h"
#include "config.h"

bool Commander::isCommand(const uint8_t* data, size_t len) {
    return data != nullptr && len >= PREFIX_LEN &&
           std::memcmp(data, PREFIX, PREFIX_LEN) == 0;
}

bool Commander::isCommand(const std::string& s) {
    return s.rfind(PREFIX, 0) == 0;      // «начинается с»
}

void Commander::handle(const std::string& body, const std::string& source) {

    // 1. Прямая передача (1 хоп)
    if (body.rfind("toss ", 0) == 0) {
        tosser(body);
    }

    if (body.rfind("eb_port", 0) == 0){
        Log::info("Commander", "EventBus port is ", config.eventBusPort);
    }


    Log::info("Commander", "[", source, "] ", body);
}

void Commander::tosser(const std::string& body){
    // Ищем первый пробел (после "toss")
    size_t firstSpace = body.find(' ');
    if (firstSpace == std::string::npos) {
        Log::error("Commander", "Неверный формат: ожидается 'toss <ip> <payload>'");
        return;
    }

    // Ищем второй пробел (после IP-адреса)
    size_t secondSpace = body.find(' ', firstSpace + 1);
    if (secondSpace == std::string::npos) {
        Log::error("Commander", "Неверный формат: отсутствует полезная нагрузка");
        return;
    }

    // Извлекаем IP и сырой payload
    // ВАЖНО: substr работает с std::string бинарно-безопасно, сохраняя любые '\0' и пробелы
    std::string destIp = body.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    std::string payloadStr = body.substr(secondSpace + 1);

    // 2. Поиск в адресной книге
    // здесь же будет искаться маршрут, если он где-то сохранен
    Addressbook::Contact destContact;
    if (!Addressbook::getInstance().findContactByIp(destIp, destContact)) {
        Log::error("Commander", "Контакт с IP '", destIp, "' не найден в адресной книге");
        return;
    }

    // 3. Преобразование в вектор байт (сырые данные)
    std::vector<uint8_t> payload(payloadStr.begin(), payloadStr.end());

    // 4. Формирование и отправка сообщения
    MsgParser::PuhegUpperMessage pumsg;
    pumsg.what = "toss";
    pumsg.howmuch = destContact.id;
    pumsg.msg = parser.encodeMsg(payload); // Используем перегрузку для vector<uint8_t>
    parser.packMessage(&pumsg);

    wsclient.sendMessage(&pumsg, false);
    Log::info("Commander", "TX toss -> IP:", destIp, " (ID:", destContact.id, ") size:", payload.size(), " bytes");
    return;
}
