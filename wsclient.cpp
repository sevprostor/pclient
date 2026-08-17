#include "wsclient.h"
#include "log.h"
#include <cstring>
#include <iostream>

static struct lws_protocols protocols[] = {
    { "qos2-protocol", WSclient::callbackQos2Client, 0, 0, 0, nullptr, 0 },
    { nullptr, nullptr, 0, 0, 0, nullptr, 0 }
};

// 1. Инициализация переменных
void WSclient::initContext() {
    this->wsi = nullptr;
    this->running = 1;
    this->lastNnc = -1;
    this->resendTries = 0;
    this->isLineBusy = false;

    std::queue<MsgParser::PuhegUpperMessage> empty;
    std::swap(this->messageBuffer, empty);
    this->sentMessage = MsgParser::PuhegUpperMessage();

    this->lastBufferCheck = std::chrono::steady_clock::now();
    this->lastResendCheck = std::chrono::steady_clock::now();
    this->lastTimeoutCheck = std::chrono::steady_clock::now();
}

// 2. Подключение к WS (ИСПРАВЛЕНО: используем this->lws_ctx_)
bool WSclient::connectWS(const std::string& wsAddr) {
    struct lws_context_creation_info info;
    std::memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.user = this; // <-- Передаем указатель на текущий объект!

    // ИСПРАВЛЕНО: сохраняем в поле класса, а не в локальную переменную
    this->lws_ctx_ = lws_create_context(&info);
    if (!this->lws_ctx_) {
        Log::error("Wsclient", "Critical: lws_create_context failed");
        return false;
    }

    struct lws_client_connect_info i;
    std::memset(&i, 0, sizeof(i));
    i.context = this->lws_ctx_; // <-- ИСПРАВЛЕНО: используем поле класса
    i.address = wsAddr.c_str();
    i.port = 80;
    i.path = "/ws";
    i.host = i.address;
    i.origin = i.address;
    i.protocol = protocols[0].name;
    i.userdata = this;

    lws_client_connect_via_info(&i);
    Log::info("Wsclient", "Инициировано подключение к ", wsAddr);
    return true;
}

// 3. Обслуживание цикла (теперь работает, т.к. lws_ctx_ инициализирован)
void WSclient::service(int timeout_ms) {
    if (this->lws_ctx_) {
        lws_service(this->lws_ctx_, timeout_ms);
    }
}

// 4. Уничтожение контекста
void WSclient::destroyContext() {
    if (this->lws_ctx_) {
        lws_context_destroy(this->lws_ctx_);
        this->lws_ctx_ = nullptr;
        Log::info("Wsclient", "Контекст libwebsockets уничтожен.");
    }
}

// ... (методы sendSuccess, successReceived, sendMessage, processTimers, extractNnc
//     оставляем БЕЗ ИЗМЕНЕНИЙ, они у вас написаны отлично) ...

void WSclient::sendSuccess(struct lws *wsiParam) {
    std::vector<uint8_t> writeBuf(LWS_PRE + 1);
    writeBuf[LWS_PRE] = 'S';
    lws_write(wsiParam, &writeBuf[LWS_PRE], 1, LWS_WRITE_BINARY);
}

void WSclient::successReceived() {
    if (this->isLineBusy) {
        this->isLineBusy = false;
        this->sentMessage = MsgParser::PuhegUpperMessage();
        this->resendTries = 0;
    }
}

void WSclient::sendMessage(MsgParser::PuhegUpperMessage *pumsg, bool forceSend) {
    if (pumsg->packedMsg.size() <= LWS_PRE) {
        Log::error("Wsclient", "packedMsg пуст или слишком мал");
        return;
    }

    size_t size = pumsg->packedMsg.size() - LWS_PRE;
    bool isDeviceBusy = MsgParser::isDeviceBusy();

    if (!forceSend && (this->isLineBusy || this->wsi == nullptr || isDeviceBusy)) {
        // --- НОВАЯ ДЕТАЛЬНАЯ ДИАГНОСТИКА ---
        if (this->wsi == nullptr) {
            Log::error("Wsclient", "ОТМЕНА: WebSocket еще не подключен (wsi == nullptr). Ждем соединения...");
        } else if (this->isLineBusy) {
            Log::info("Wsclient", "ОЖИДАНИЕ: Линия занята (ждем 'S' от устройства). Пакет в буфер.");
        } else if (isDeviceBusy) {
            Log::info("Wsclient", "ОЖИДАНИЕ: Устройство выполняет процесс. Пакет в буфер.");
        }
        // ------------------------------------

        const size_t MAX_QUEUE = 64; //перенести в wsclient.conf

        if (this->messageBuffer.size() >= MAX_QUEUE) {
            this->messageBuffer.pop();  // выбрасываем самый старый пакет
            Log::info("Wsclient", "Очередь переполнена, старый пакет отброшен");
        }

        this->messageBuffer.push(*pumsg);
        return;
    }

    if (!isDeviceBusy) {
        MsgParser::startProcess(pumsg->thread);
    }

    lws_write(this->wsi, &pumsg->packedMsg[LWS_PRE], size, LWS_WRITE_BINARY);
    Log::info("Wsclient", "Сообщение отправлено в станцию");

    this->sentMessage = *pumsg;
    this->isLineBusy = true;

    if (!forceSend) {
        this->resendTries = 0;
    }
}

void WSclient::processTimers() {
    auto currentTime = std::chrono::steady_clock::now();

    auto passedBufferMs = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - this->lastBufferCheck).count();

    //проверка буфера неотправленных сообщений
    if (passedBufferMs >= 1000) { //было 500
        this->lastBufferCheck = currentTime;
        if (!this->isLineBusy && this->wsi != nullptr && !this->messageBuffer.empty() && !MsgParser::isDeviceBusy()) { //добавлено !MsgParser::isDeviceBusy()
            MsgParser::PuhegUpperMessage nextMsg = this->messageBuffer.front();
            this->messageBuffer.pop();
            sendMessage(&nextMsg, false);
        }
    }

    auto passedResendMs = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - this->lastResendCheck).count();
    if (passedResendMs >= 1000) {
        this->lastResendCheck = currentTime;
        if (this->isLineBusy && this->wsi != nullptr && this->sentMessage.id != 0) {
            if (this->resendTries < 3) {
                this->resendTries++;
                sendMessage(&this->sentMessage, true);
            } else {
                Log::error("Wsclient", "Превышено число попыток. Пакет стерт из памяти.");
                this->isLineBusy = false;
                this->sentMessage = MsgParser::PuhegUpperMessage();
                this->resendTries = 0;
            }
        }
    }

    static std::chrono::steady_clock::time_point lastTimeoutCheck = std::chrono::steady_clock::now();
    auto passedTimeoutMs = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastTimeoutCheck).count();
    if (passedTimeoutMs >= 500) {
        lastTimeoutCheck = currentTime;
        MsgParser::checkProcessTimeout();
    }
}

int64_t WSclient::extractNnc(const msgpack::object& obj) {
    if (obj.type != msgpack::type::MAP) return -1;
    std::map<std::string, msgpack::object> root_map;
    obj.convert(root_map);
    for (const auto& pair : root_map) {
        if (pair.second.type == msgpack::type::MAP) {
            std::map<std::string, msgpack::object> inner_map;
            pair.second.convert(inner_map);
            auto it = inner_map.find("nnc");
            if (it != inner_map.end()) {
                return it->second.as<int64_t>();
            }
        }
    }
    return -1;
}

// 5. Callback (ИСПРАВЛЕНО: безопасное использование указателя this)
int WSclient::callbackQos2Client(struct lws *wsi, enum lws_callback_reasons reason,
                                 void *user, void *in, size_t len) {
    // Получаем указатель на наш объект из параметра user (мы передали его в info.user = this)
    //WSclient* client = static_cast<WSclient*>(user);
    WSclient* client = static_cast<WSclient*>(lws_wsi_user(wsi));
    if (!client) {
        // Fallback: пробуем получить из контекста
        client = static_cast<WSclient*>(lws_context_user(lws_get_context(wsi)));
    }

    if (!client) return 0;

    switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        Log::info("Wsclient", "Подключение установлено");
        client->wsi = wsi;
        client->isLineBusy = false;
        break;

    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        Log::error("Wsclient", "Ошибка подключения");
        client->wsi = nullptr;
        client->isLineBusy = false;
        break;

    case LWS_CALLBACK_CLIENT_CLOSED:
        Log::info("Wsclient", "Соединение закрыто");
        client->wsi = nullptr;
        client->isLineBusy = false;
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE:
        if (len == 0 || in == nullptr) break;

        if (len == 1 && *(char*)in == 'S') {
            client->successReceived();
        } else {
            if (client->isLineBusy) {
                client->isLineBusy = false;
                client->sentMessage = MsgParser::PuhegUpperMessage();
                client->resendTries = 0;
                Log::info("QoS2", "Получены данные от устройства. Линия свободна.");
            }

            client->sendSuccess(wsi);

            try {
                msgpack::object_handle oh = msgpack::unpack(reinterpret_cast<const char*>(in), len);
                msgpack::object deserialized = oh.get();

                int64_t current_nnc = client->extractNnc(deserialized);
                if (current_nnc != -1) {
                    if (current_nnc == client->lastNnc) {
                        return 0; // Дубликат
                    }
                    client->lastNnc = current_nnc;
                }

                MsgParser::dispatchIncomingPacket(deserialized);

            } catch (const std::exception& e) {
                Log::error("Wsclient", "Ошибка десериализации MsgPack");
            }
        }
        break;

    case LWS_CALLBACK_CLIENT_WRITEABLE:
        break;

    default:
        break;
    }
    return 0;
}
