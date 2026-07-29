#include "msgparser.h"
#include "addressbook.h" // Обязательный инклуд для работы с адресной книгой
#include <sstream>
#include <cstdlib>
#include <map>
#include <string>

// Структура строго повторяет поля словаря cmd из Python консоли + ID
struct SystemPacket {
    std::string what;
    std::string todo;
    int32_t howmuch;            // Поддерживает знаковые числа, включая -1
    msgpack::type::raw_ref msg; // Сырые байты данных
    uint32_t id;
    uint32_t thread;

    MSGPACK_DEFINE_MAP(what, todo, howmuch, msg, id, thread);
};

void MsgParser::dispatchIncomingPacket(const msgpack::object& obj) {
    // Если пришел не словарь (Map), выходим — это некорректный системный пакет
    if (obj.type != msgpack::type::MAP) return;

    // Конвертируем корень во временную C++ карту для независимого разбора блоков (аналог data.get() в Python)
    std::map<std::string, msgpack::object> root_map;
    obj.convert(root_map);

    //ВЫВОД СЫРОГО JSON НАПРЯМУЮ
    //Без всяких условий
    std::cout << "\n========================================" << std::endl;
    std::cout << "[MsgParser] Входящее сообщение:" << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    std::cout << obj << std::endl; // Выводит дерево элементов одной строкой кода
    std::cout << "========================================\n" << std::endl;

    // =========================================================================
    // БЛОК 1: ADDRESSBOOK (Аналог Python: if isinstance(adressbook, dict))
    // =========================================================================
    auto ab_it = root_map.find("addressbook");
    //не понимаю, уточнить
    if (ab_it != root_map.end() && ab_it->second.type == msgpack::type::MAP) {
        std::map<std::string, msgpack::object> ab_map;
        ab_it->second.convert(ab_map);

        // А. Обработка единичного контакта (addressbook.record)
        auto rec_it = ab_map.find("record");
        if (rec_it != ab_map.end() && rec_it->second.type == msgpack::type::MAP) {
            std::map<std::string, msgpack::object> rec_map;
            rec_it->second.convert(rec_map);

            Contact c;
            if (rec_map.count("id")) c.id = static_cast<uint16_t>(rec_map["id"].as<uint64_t>());

            // Безопасное приведение к uint8_t
            if (rec_map.count("chanComm")) c.chanComm = static_cast<uint8_t>(rec_map["chanComm"].as<uint64_t>());
            if (rec_map.count("netsp")) c.netsp = static_cast<uint8_t>(rec_map["netsp"].as<uint64_t>());

            if (rec_map.count("key")) c.key = rec_map["key"].as<std::string>();

            // Проверяем флаг myOwn (флаг того, что это профиль нашего текущего интерфейса)
            bool my_own = rec_map.count("myOwn") ? (rec_map["myOwn"].as<int32_t>() != 0) : false;

            if (my_own) {
                Addressbook::getInstance().setMyProfile(c.id, c);
            } else {
                Addressbook::getInstance().setContact(c.id, c);
            }
        }

        // Б. Обработка полного списка контактов (addressbook.contacts)
        auto contacts_it = ab_map.find("contacts");
        if (contacts_it != ab_map.end() && contacts_it->second.type == msgpack::type::MAP) {
            std::map<std::string, msgpack::object> contacts_map;
            contacts_it->second.convert(contacts_map);

            // Временный C++ словарь для пакетной загрузки контактов (ключ — uint16_t)
            std::map<uint16_t, Contact> parsed_batch;

            for (const auto& [key, contact_obj] : contacts_map) {
                if (contact_obj.type != msgpack::type::MAP) continue;

                std::map<std::string, msgpack::object> c_map;
                contact_obj.convert(c_map);

                Contact c;
                // Если внутри структуры есть числовой "id" — берем его, иначе парсим строковый ключ ("221" -> 221)
                if (c_map.count("id")) {
                    c.id = static_cast<uint16_t>(c_map["id"].as<uint64_t>());
                } else {
                    c.id = static_cast<uint16_t>(std::stoul(key));
                }

                // Безопасное приведение к uint8_t
                if (c_map.count("chanComm")) c.chanComm = static_cast<uint8_t>(c_map["chanComm"].as<uint64_t>());
                if (c_map.count("netsp")) c.netsp = static_cast<uint8_t>(c_map["netsp"].as<uint64_t>());

                if (c_map.count("name")) c.name = c_map["name"].as<std::string>();
                else if (c_map.count("label")) c.name = c_map["label"].as<std::string>();


                parsed_batch[c.id] = c;
            }

            // Сохраняем весь пакет контактов в память синглтона Addressbook
            Addressbook::getInstance().setContacts(parsed_batch);

            // Запрашиваем у адресной книги красивый вывод итоговой таблицы в консоль
            Addressbook::getInstance().print();
        }
    }

    // =========================================================================
    // БЛОК 2: PROCESS (Аналог Python: if isinstance(proc, dict))
    // =========================================================================
    auto proc_it = root_map.find("process");
    if (proc_it != root_map.end() && proc_it->second.type == msgpack::type::MAP) {
        std::map<std::string, msgpack::object> proc_map;
        proc_it->second.convert(proc_map);

        std::string state = proc_map.count("state") ? proc_map["state"].as<std::string>() : "";
        std::string thread = proc_map.count("thread") ? proc_map["thread"].as<std::string>() : "";

        std::cout << "[MsgParser] Обработка шага процесса '" << thread << "' -> Статус: " << state << std::endl;

        if (state == "OK") {
            //std::cout << "[MsgParser] >>> СЕССИЯ ИНИЦИАЛИЗАЦИИ БЛАГОПОЛУЧНО ЗАВЕРШЕНА <<<" << std::endl;
        }
    }
}




void MsgParser::printPretty(const msgpack::object& obj, int indent) {
    std::string spaces(indent, ' ');
    switch (obj.type) {
    case msgpack::type::NIL:              std::cout << "null"; break;
    case msgpack::type::BOOLEAN:          std::cout << (obj.via.boolean ? "true" : "false"); break;
    case msgpack::type::POSITIVE_INTEGER: std::cout << obj.via.u64; break;
    case msgpack::type::NEGATIVE_INTEGER: std::cout << obj.via.i64; break;
    case msgpack::type::FLOAT32:
    case msgpack::type::FLOAT64:          std::cout << obj.via.f64; break;
    case msgpack::type::STR:              std::cout << "\"" << obj.as<std::string>() << "\""; break;
    case msgpack::type::BIN:              std::cout << "<binary data, size: " << obj.via.bin.size << " bytes>"; break;
    case msgpack::type::ARRAY:
        std::cout << "[\n";
        for (uint32_t i = 0; i < obj.via.array.size; ++i) {
            std::cout << spaces << "  "; printPretty(obj.via.array.ptr[i], indent + 2);
            if (i + 1 < obj.via.array.size) std::cout << ","; std::cout << "\n";
        }
        std::cout << spaces << "]"; break;
    case msgpack::type::MAP:
        std::cout << "{\n";
        for (uint32_t i = 0; i < obj.via.map.size; ++i) {
            auto& kv = obj.via.map.ptr[i];
            std::cout << spaces << "  "; printPretty(kv.key, indent + 2); std::cout << ": ";
            printPretty(kv.val, indent + 2);
            if (i + 1 < obj.via.map.size) std::cout << ","; std::cout << "\n";
        }
        std::cout << spaces << "}"; break;
    default: std::cout << "<unknown type>"; break;
    }
    if (indent == 0) std::cout << std::endl;
}

int64_t MsgParser::extractNnc(const uint8_t *data, size_t size) {
    int64_t nnc = -1;
    try {
        msgpack::object_handle oh = msgpack::unpack((const char*)data, size);
        msgpack::object deserialized = oh.get();
        if (deserialized.type == msgpack::type::MAP && deserialized.via.map.size > 0) {
            msgpack::object first_value = deserialized.via.map.ptr->val;
            if (first_value.type == msgpack::type::MAP) {
                std::map<std::string, msgpack::object> inner_map;
                first_value.convert(inner_map);
                auto it = inner_map.find("nnc");
                if (it != inner_map.end() && it->second.type == msgpack::type::POSITIVE_INTEGER) {
                    nnc = it->second.via.u64;
                }
            }
        }
    } catch (...) {}
    return nnc;
}

/*
std::vector<uint8_t> MsgParser::packMessage(const std::string& what,
                                            const std::string& todo,
                                            int32_t howmuch,
                                            const std::vector<uint8_t>& msg)
{
    SystemPacket packet;
    packet.what = what;
    packet.todo = todo;
    packet.howmuch = howmuch;
    packet.msg = msgpack::type::raw_ref((const char*)msg.data(), msg.size());
    packet.id = std::rand() % 10000000;

    std::stringstream buffer;
    msgpack::pack(buffer, packet);
    std::string str_data = buffer.str();

    std::vector<uint8_t> result(LWS_PRE + str_data.size());
    std::memcpy(&result[LWS_PRE], str_data.data(), str_data.size());
    return result;
}
*/

std::vector<uint8_t> MsgParser::packMessage(PuhegUpperMessage *pumsg)
{
    SystemPacket packet;
    packet.what = pumsg->what;
    packet.todo = pumsg->todo;
    packet.howmuch = pumsg->howmuch;
    packet.msg = msgpack::type::raw_ref((const char*)pumsg->msg.data(), pumsg->msg.size());
    packet.id = std::rand() % 10000000;
    packet.thread = pumsg->thread;

    std::stringstream buffer;
    msgpack::pack(buffer, packet);
    std::string str_data = buffer.str();

    std::vector<uint8_t> result(LWS_PRE + str_data.size());
    std::memcpy(&result[LWS_PRE], str_data.data(), str_data.size());

    // ВЫВОД СФОРМИРОВАННОГО JSON НА ЭКРАН
    msgpack::object_handle oh = msgpack::unpack(str_data.data(), str_data.size());
    std::cout << "[MsgParser] Исходящее сообщение: " << oh.get() << std::endl;

    return result;
}

std::vector<uint8_t> MsgParser::parseFlatCommand(const std::string& flat_line) {
    std::string what = "";
    std::string todo = "";
    int32_t howmuch = -1;
    std::vector<uint8_t> msg_bytes;

    MsgParser::PuhegUpperMessage pumsg;

    try {
        // Ищем позиции трех точек-разделителей
        size_t p1 = flat_line.find('.');
        size_t p2 = (p1 != std::string::npos) ? flat_line.find('.', p1 + 1) : std::string::npos;
        size_t p3 = (p2 != std::string::npos) ? flat_line.find('.', p2 + 1) : std::string::npos;

        // Если формат нарушен (нет хотя бы 3 точек), пакуем всю строку как ошибку в поле what
        if (p1 == std::string::npos || p2 == std::string::npos || p3 == std::string::npos) {
            std::cerr << "[Parser Error] Неверный формат! Ожидалось: what.todo.howmuch. msg" << std::endl;
            //return packMessage("error", "invalid_format", -1, std::vector<uint8_t>(flat_line.begin(), flat_line.end()));
            pumsg.what = "error";
            pumsg.todo = "invalid_format";
            pumsg.msg = std::vector<uint8_t>(flat_line.begin(), flat_line.end());

        } else {

            // Вырезаем строки между точками
            pumsg.what = flat_line.substr(0, p1);
            pumsg.todo = flat_line.substr(p1 + 1, p2 - p1 - 1);

            std::string howmuch_str = flat_line.substr(p2 + 1, p3 - p2 - 1);
            if (!howmuch_str.empty()) {
                pumsg.howmuch = std::stoi(howmuch_str);
            }

            // После третьей точки идет пробел и само сообщение: ". hello" -> берем всё после ". "
            if (p3 + 2 < flat_line.size()) {
                std::string msg_str = flat_line.substr(p3 + 2); // Пропускаем точку и пробел
                msg_bytes.assign(msg_str.begin(), msg_str.end());
                pumsg.msg = msg_bytes;
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[Parser Error] Исключение при разборе: " << e.what() << std::endl;
        //return packMessage("error", "parser_exception", -1, {});
        pumsg.what = "error";
        pumsg.todo = "parser_exception";
    }

    // Возвращаем упакованный по правилам C++ MsgPack-пакет (с префиксом LWS_PRE)
    return packMessage(&pumsg);
}

