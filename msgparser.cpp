#include "msgparser.h"
#include "addressbook.h" // Обязательный инклуд для работы с адресной книгой
#include "log.h"
#include <sstream>
#include <cstdlib>
#include <map>
#include <string>

MsgParser::RunningProcess runningProc;

void MsgParser::dispatchIncomingPacket(const msgpack::object& obj) {
    // Если пришел не словарь (Map), выходим — это некорректный системный пакет
    if (obj.type != msgpack::type::MAP) return;

    // Конвертируем корень во временную C++ карту для независимого разбора блоков (аналог data.get() в Python)
    std::map<std::string, msgpack::object> root_map;
    obj.convert(root_map);

    //ВЫВОД СЫРОГО JSON НАПРЯМУЮ
    //Без всяких условий
    //std::cout << "\n========================================" << std::endl;
    //std::cout << "[MsgParser] Входящее сообщение:" << std::endl;
    //std::cout << "----------------------------------------" << std::endl;
    //std::cout << obj << std::endl; // Выводит дерево элементов одной строкой кода
    //std::cout << "========================================\n" << std::endl;
    Log::info("Msgparser", "[IN <<<]", obj);

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

            Addressbook::Contact c;
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
            std::map<uint16_t, Addressbook::Contact> parsed_batch;

            for (const auto& [key, contact_obj] : contacts_map) {
                if (contact_obj.type != msgpack::type::MAP) continue;

                std::map<std::string, msgpack::object> c_map;
                contact_obj.convert(c_map);

                Addressbook::Contact c;
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

        //std::cout << "[MsgParser] Обработка шага процесса '" << thread << "' -> Статус: " << state << std::endl;

        if(runningProc.justLaunched){
            Log::info("Process", "Новый процесс ", thread, " зарегистрирован как активный.");
        }

        runningProc.justLaunched = false;

        watchProcess(state, thread);

    } else if(runningProc.running && runningProc.justLaunched){
        //если в только запущенном процесс нету process - просто сбросить.

        runningProc.running = false;
        //Log::info("Msgparser", "Это не процесс, разблокировано");

    }

    /*
    for (const auto& [key, value] : root_map) {
        // Нас интересуют только вложенные словари (MAP)
        if (value.type == msgpack::type::MAP) {
            std::map<std::string, msgpack::object> inner_map;
            value.convert(inner_map);

            std::string state = "";
            std::string thread_or_parent = "";

            // Вариант А: Это полноценный блок "process" (есть поле "thread")
            if (inner_map.count("thread")) {
                thread_or_parent = inner_map["thread"].as<std::string>();
                if (inner_map.count("state")) {
                    state = inner_map["state"].as<std::string>();
                }
            }
            // Вариант Б: Это блок задачи типа "transport" (есть поле "parent")
            else if (inner_map.count("parent")) {
                thread_or_parent = inner_map["parent"].as<std::string>();
                if (inner_map.count("state")) {
                    state = inner_map["state"].as<std::string>();
                }
            }

            // Если мы нашли и идентификатор, и состояние, передаем их в монитор
            if (!state.empty() && !thread_or_parent.empty()) {
                watchProcess(state, thread_or_parent);
            }
        }
    }*/
}



/*
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
}*/

// Кодировка обычного текста
std::vector<int> MsgParser::encodeMsg(const std::string& text) {
    return std::vector<int>(text.begin(), text.end());
}

// Кодировка сырых байт (на будущее, для IP-пакетов)
std::vector<int> MsgParser::encodeMsg(const std::vector<uint8_t>& rawBytes) {
    return std::vector<int>(rawBytes.begin(), rawBytes.end());
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
    //std::cout << "[MsgParser] Исходящее сообщение: " << oh.get() << std::endl;
    Log::info("Msgparser", "[OUT >>>]", oh.get());
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
            //std::cerr << "[Parser Error] Неверный формат! Ожидалось: what.todo.howmuch. msg" << std::endl;
            Log::error("Msgparser", "Неверный формат! Ожидалось: what.todo.howmuch. msg");

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
                pumsg->msg = encodeMsg(msg_str);
            }
        }
    }
    catch (const std::exception& e) {
        //std::cerr << "[Parser Error] Исключение при разборе: " << e.what() << std::endl;
        Log::error("Msgparser", "parse error");

    }


    parser.packMessage(pumsg);
    //return pumsg.packedMsg;
}

// =========================================================================
// УПРАВЛЕНИЕ СОСТОЯНИЕМ ПРОЦЕССА
// =========================================================================

bool MsgParser::isDeviceBusy() {
    return runningProc.running;
}

void MsgParser::startProcess(uint32_t threadId) {
    runningProc.thread = threadId;
    runningProc.running = true;
    runningProc.justLaunched = true;
    runningProc.startTime = std::chrono::steady_clock::now();
    runningProc.lastResponseTime = std::chrono::steady_clock::time_point(); // Сброс в ноль
    //Log::info("Process", "Новый процесс ", threadId, " зарегистрирован как активный.");
}

/*
void MsgParser::watchProcess(const std::string& state, const std::string& thread) {
    if (!runningProc.running) {
        return; // Ничего не отслеживаем, выходим
    }

    // Преобразуем числовой ID в строку для безопасного сравнения
    std::string trackedThreadStr = std::to_string(runningProc.thread);



    if (trackedThreadStr == thread) {

        Log::info("Msgparser", "Process ", runningProc.thread, ": ", state);

        //if (state == "WORK") {
            //std::cout << "[Process] Процесс '" << thread << "' выполняется (WORK)..." << std::endl;

        //}
        if (state == "OK") {
            //std::cout << "[Process] >>> Процесс '" << thread << "' успешно завершен (OK)! <<<" << std::endl;
            Log::info("Msgparser", "Процесс успешно завершен");
            runningProc.running = false; // Освобождаем устройство
        }
        else if (state == "FAIL") {
            //std::cerr << "[Process] !!! Процесс '" << thread << "' завершился с ошибкой (FAIL)! <<<" << std::endl;
            Log::info("Msgparser", "Процесс провален");
            runningProc.running = false; // Освобождаем устройство даже при ошибке, чтобы избежать deadlock
        }
    }
}*/

void MsgParser::watchProcess(const std::string& state, const std::string& threadId) {
    if (!runningProc.running) return;

    std::string trackedThreadStr = std::to_string(runningProc.thread);

    if (trackedThreadStr == threadId) {
        if (state == "WORK") {

            Log::info("Process", "Процесс '", threadId, "' выполняется (WORK)...");
            runningProc.lastResponseTime = std::chrono::steady_clock::now(); // <-- ДОБАВЛЕНО
        }
        else if (state == "OK") {
            Log::info("Process", ">>> Процесс '", threadId, "' успешно завершен (OK)! <<<");
            runningProc.running = false;
        }
        else if (state == "FAIL") {
            Log::error("Process", "!!! Процесс '", threadId, "' завершился с ошибкой (FAIL)! <<<");
            runningProc.running = false;
        }
    }
}

void MsgParser::checkProcessTimeout() {
    if (!runningProc.running) return;

    auto currentTime = std::chrono::steady_clock::now();

    // Если ответ (WORK) еще не приходил, проверяем таймаут первого ответа (1000 мс)
    if (runningProc.lastResponseTime.time_since_epoch().count() == 0) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           currentTime - runningProc.startTime).count();
        if (elapsed >= 1000) {
            Log::error("Process", "Таймаут первого ответа (1000 мс) для процесса ", runningProc.thread);
            runningProc.running = false;
        }
    }
    // Если ответ (WORK) уже приходил, проверяем таймаут завершения (5000 мс)
    else {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           currentTime - runningProc.lastResponseTime).count();
        if (elapsed >= 8000) {
            Log::error("Process", "Таймаут завершения (5000 мс) для процесса ", runningProc.thread);
            runningProc.running = false;
        }
    }
}
