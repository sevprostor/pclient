#include "msgparser.h"
#include "addressbook.h" // Обязательный инклуд для работы с адресной книгой
#include "log.h"
#include "tun.h"
#include "eventbus.h"

#include <sstream>
#include <cstdlib>
#include <map>
#include <string>

#include "logcolors.h"
#include "commander.h"

extern TunInterface tun;

MsgParser::RunningProcess runningProc;

#include "libs/json.hpp"

// Рекурсивный конвертер msgpack::object -> nlohmann::json.
// Ключевое: BIN -> массив целых, чтобы emit видел реальные байты PW-кадра.
static nlohmann::json msgpackToJson(const msgpack::object& o) {
    using nlohmann::json;
    switch (o.type) {
    case msgpack::type::NIL:      return nullptr;
    case msgpack::type::BOOLEAN:  return o.via.boolean;
    case msgpack::type::POSITIVE_INTEGER: return o.via.u64;
    case msgpack::type::NEGATIVE_INTEGER: return o.via.i64;
    case msgpack::type::FLOAT32:
    case msgpack::type::FLOAT64:  return o.via.f64;
    case msgpack::type::STR:
        return std::string(o.via.str.ptr, o.via.str.size);
    case msgpack::type::BIN: {
        json arr = json::array();
        for (uint32_t i = 0; i < o.via.bin.size; ++i)
            arr.push_back(static_cast<uint8_t>(o.via.bin.ptr[i]));
        return arr;
    }
    case msgpack::type::ARRAY: {
        json arr = json::array();
        for (uint32_t i = 0; i < o.via.array.size; ++i)
            arr.push_back(msgpackToJson(o.via.array.ptr[i]));
        return arr;
    }
    case msgpack::type::MAP: {
        json m = json::object();
        for (uint32_t i = 0; i < o.via.map.size; ++i) {
            const auto& kv = o.via.map.ptr[i];
            std::string key = (kv.key.type == msgpack::type::STR)
                                  ? std::string(kv.key.via.str.ptr, kv.key.via.str.size)
                                  : std::to_string(i);
            m[key] = msgpackToJson(kv.val);
        }
        return m;
    }
    default: return nullptr;
    }
}

void MsgParser::dispatchIncomingPacket(const msgpack::object& obj) {
    // Если пришел не словарь (Map), выходим — это некорректный системный пакет
    if (obj.type != msgpack::type::MAP) return;

    // Каждое входящее puheg-сообщение целиком уходит в шину.
    // EventBus сам решит, это событие или PW-кадр.
    {
        std::ostringstream jss;
        jss << obj;
        EventBus::emit(msgpackToJson(obj).dump());
    }

    std::map<std::string, msgpack::object> root_map;
    obj.convert(root_map);

    //ВЫВОД СЫРОГО JSON НАПРЯМУЮ
    Log::info("Msgparser", TAG_IN, obj);

    // =========================================================================
    // БЛОК 1: ADDRESSBOOK
    // =========================================================================
    auto thisIsAddressbook = root_map.find("addressbook");
    //не понимаю, уточнить
    if (thisIsAddressbook != root_map.end() && thisIsAddressbook->second.type == msgpack::type::MAP) {
        std::map<std::string, msgpack::object> ab_map;
        thisIsAddressbook->second.convert(ab_map);

        // А. Обработка единичного контакта (addressbook.record)
        auto thisIsABRec = ab_map.find("record");
        if (thisIsABRec != ab_map.end() && thisIsABRec->second.type == msgpack::type::MAP) {
            std::map<std::string, msgpack::object> rec_map;
            thisIsABRec->second.convert(rec_map);

            Addressbook::Contact c;
            if (rec_map.count("id")) c.id = static_cast<uint16_t>(rec_map["id"].as<uint64_t>());

            // Безопасное приведение к uint8_t
            if (rec_map.count("chanComm")) c.chanComm = static_cast<uint8_t>(rec_map["chanComm"].as<uint64_t>());
            if (rec_map.count("netsp")) c.netsp = static_cast<uint8_t>(rec_map["netsp"].as<uint64_t>());
            if (rec_map.count("netSpeed")) c.netsp = static_cast<uint8_t>(rec_map["netSpeed"].as<uint64_t>());

            if (rec_map.count("key")) c.key = rec_map["key"].as<std::string>();

            // Проверяем флаг myOwn (флаг того, что это профиль нашего текущего интерфейса)
            bool my_own = rec_map.count("myOwn") ? (rec_map["myOwn"].as<int32_t>() != 0) : false;

            if (my_own) {
                c.myOwn = true;
                Addressbook::getInstance().setMyProfile(c.id, c);
            } else {
                Addressbook::getInstance().setContact(c.id, c);
            }
        }

        // Б. Обработка полного списка контактов (addressbook.contacts)
        auto thisIsFullAB = ab_map.find("contacts");
        if (thisIsFullAB != ab_map.end() && thisIsFullAB->second.type == msgpack::type::MAP) {
            std::map<std::string, msgpack::object> contacts_map;
            thisIsFullAB->second.convert(contacts_map);

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
    // БЛОК 2: PROCESS
    // =========================================================================

    auto thisIsProcess = root_map.find("process");
    if (thisIsProcess != root_map.end() && thisIsProcess->second.type == msgpack::type::MAP) {
        std::map<std::string, msgpack::object> proc_map;
        thisIsProcess->second.convert(proc_map);

        std::string state = proc_map.count("state") ? proc_map["state"].as<std::string>() : "";
        std::string thread = proc_map.count("thread") ? proc_map["thread"].as<std::string>() : "";

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

    // Обработка транспорта
    auto thisIsTransport = root_map.find("transport");
    if (thisIsTransport != root_map.end() && thisIsTransport->second.type == msgpack::type::MAP) {
        std::map<std::string, msgpack::object> transport_map;
        thisIsTransport->second.convert(transport_map);

        //Ищем парент для отправки в watchProcess() чтобы не происходило
        //сброса процесса раньше времени на долгих отправках.
        //Это нужно сделать для всех типов сообщений.
        std::string thread = transport_map.count("parent") ? transport_map["parent"].as<std::string>() : "";
        watchProcess("WORK", thread);

        // Ищем downlink
        auto thisIsDownlink = transport_map.find("downlink");
        if (thisIsDownlink != transport_map.end() && thisIsDownlink->second.type == msgpack::type::MAP) {

            // === НОВОЕ: страж входящей передачи (только downlink!) ===
            //Это про блокировку при входящем транспорте
            std::string tr_state = transport_map.count("state")
                                       ? transport_map["state"].as<std::string>() : "";
            uint32_t tr_id = transport_map.count("id")
                                 ? static_cast<uint32_t>(transport_map["id"].as<uint64_t>()) : 0;
            MsgParser::watchTransport(tr_state, tr_id);
            // === конец нового ===

            std::map<std::string, msgpack::object> downlink_map;
            thisIsDownlink->second.convert(downlink_map);



            // Ищем packet
            auto thisIsPacket = downlink_map.find("packet");
            if (thisIsPacket != downlink_map.end()) {
                // Извлекаем бинарные данные
                std::vector<uint8_t> packetBytes;

                if (thisIsPacket->second.type == msgpack::type::BIN) {
                    msgpack::type::raw_ref raw;
                    thisIsPacket->second.convert(raw);
                    packetBytes.assign(raw.ptr, raw.ptr + raw.size);
                } else if (thisIsPacket->second.type == msgpack::type::ARRAY) {
                    // Если прошивка отправила как массив байтов
                    thisIsPacket->second.convert(packetBytes);
                } else {
                    Log::info("MsgParser", "downlink.packet имеет неожиданный тип: ", (int)thisIsPacket->second.type);
                }



                // Записываем в TUN, если пакет не пустой
                // Записываем в TUN, если пакет не пустой
                if (!packetBytes.empty()) {
                    Log::info("MsgParser", "📥 Принят пакет, ", packetBytes.size(), " байт");



                    // 2. Проверка на текстовую команду драйвера (>>> ...)
                    if (Commander::isCommand(packetBytes.data(), packetBytes.size())) {
                        std::string body(reinterpret_cast<const char*>(packetBytes.data()) + Commander::PREFIX_LEN,
                                         packetBytes.size() - Commander::PREFIX_LEN);
                        commander.handle(body, "radio");
                        return; // В TUN не пишем!
                    }

                    // 3. Обычный IP-пакет -> в TUN
                    if (tun.writePacket(packetBytes.data(), packetBytes.size())) {
                        Log::info("MsgParser", "Пакет передан в tun0");
                    } else {
                        Log::error("MsgParser", "Не удалось записать пакет в TUN");
                    }
                }
            }
        }
    }
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

    msgpack::sbuffer sbuf;
    msgpack::pack(sbuf, *pumsg);

    // БЕЗ LWS_PRE
    pumsg->packedMsg.resize(sbuf.size());
    std::memcpy(pumsg->packedMsg.data(), sbuf.data(), sbuf.size());

    msgpack::object_handle oh = msgpack::unpack(sbuf.data(), sbuf.size());
    Log::info("Msgparser", TAG_OUT, oh.get());
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

            //EventBus::emit("toss_ok", "\"thread\":" + threadId + "\"");
            runningProc.running = false;
        }
        else if (state == "FAIL") {
            Log::error("Process", "!!! Процесс '", threadId, "' завершился с ошибкой (FAIL)! <<<");
            //EventBus::emit("toss_fail", "\"thread\":" + threadId + "\"");
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

MsgParser::RxTransaction rxTransaction;

bool MsgParser::isRxBusy() {
    return rxTransaction.active;
}

void MsgParser::watchTransport(const std::string& state, uint32_t id) {
    if (state == "WORK") {
        if (!rxTransaction.active) {
            Log::info("Process", "📥 Входящая транзакция ", id, " началась. TX заблокирован.");
        }
        rxTransaction.active = true;
        rxTransaction.id = id;
        rxTransaction.lastMsgTime = std::chrono::steady_clock::now();
    } else if (state == "OK" || state == "FAIL") {
        if (rxTransaction.active) {
            Log::info("Process", "📥 Входящая транзакция ", id, " завершена (", state, "). TX разблокирован.");
        }
        rxTransaction.active = false;
    }
}

void MsgParser::checkRxTimeout() {
    if (!rxTransaction.active) return;

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - rxTransaction.lastMsgTime).count();
    if (elapsed >= RX_TIMEOUT_MS) {
        Log::error("Process", "Входящая транзакция ", rxTransaction.id,
                   " затихла на ", elapsed, " мс. Принудительная разблокировка.");
        rxTransaction.active = false;
    }
}
