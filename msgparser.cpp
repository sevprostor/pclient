#include "msgparser.h"
#include "addressbook.h" // Обязательный инклуд для работы с адресной книгой
#include <sstream>
#include <cstdlib>
#include <map>
#include <string>

//MsgParser::RunningProcess runningProcess;

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



void MsgParser::packMessage(PuhegUpperMessage *pumsg) {

    pumsg->id = std::rand() % 10000000;
    pumsg->thread = std::rand() % 10000000;

    // 2. Сериализуем структуру напрямую в msgpack-буфер
    msgpack::sbuffer sbuf;
    msgpack::pack(sbuf, *pumsg);

    // 3. Выделяем память в целевом векторе: отступ LWS_PRE + размер полезных данных msgpack
    pumsg->packedMsg.resize(LWS_PRE + sbuf.size());

    // 4. Копируем данные со смещением LWS_PRE
    // sbuf.data() и sbuf.size() дают прямой доступ к упакованным байтам
    std::memcpy(pumsg->packedMsg.data() + LWS_PRE, sbuf.data(), sbuf.size());

    // 5. ВЫВОД СФОРМИРОВАННОГО JSON НА ЭКРАН (для отладки)
    // Распаковываем только что созданный буфер, чтобы красиво вывести его в консоль
    msgpack::object_handle oh = msgpack::unpack(sbuf.data(), sbuf.size());
    std::cout << "[MsgParser] Исходящее сообщение: " << oh.get() << std::endl;
}

void MsgParser::parseFlatCommand(const std::string& flat_line, PuhegUpperMessage *pumsg) {

    std::vector<uint8_t> msg_bytes;

    //MsgParser::PuhegUpperMessage pumsg;

    try {
        // Ищем позиции трех точек-разделителей
        size_t p1 = flat_line.find('.');
        size_t p2 = (p1 != std::string::npos) ? flat_line.find('.', p1 + 1) : std::string::npos;
        size_t p3 = (p2 != std::string::npos) ? flat_line.find('.', p2 + 1) : std::string::npos;

        // Если формат нарушен (нет хотя бы 3 точек), пакуем всю строку как ошибку в поле what
        if (p1 == std::string::npos || p2 == std::string::npos || p3 == std::string::npos) {
            std::cerr << "[Parser Error] Неверный формат! Ожидалось: what.todo.howmuch. msg" << std::endl;

        } else {

            // Вырезаем строки между точками
            pumsg->what = flat_line.substr(0, p1);
            pumsg->todo = flat_line.substr(p1 + 1, p2 - p1 - 1);

            std::string howmuch_str = flat_line.substr(p2 + 1, p3 - p2 - 1);
            if (!howmuch_str.empty()) {
                pumsg->howmuch = std::stoi(howmuch_str);
            }

            // После третьей точки идет пробел и само сообщение: ". hello" -> берем всё после ". "
            if (p3 + 2 < flat_line.size()) {
                std::string msg_str = flat_line.substr(p3 + 2); // Пропускаем точку и пробел
                msg_bytes.assign(msg_str.begin(), msg_str.end());
                pumsg->msg = msg_bytes;
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[Parser Error] Исключение при разборе: " << e.what() << std::endl;

    }


    parser.packMessage(pumsg);
    //return pumsg.packedMsg;
}

