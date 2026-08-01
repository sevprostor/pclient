#include "wsclient.h"
#include "log.h"

// Глобальный экземпляр (должен совпадать с тем, что в main.cpp)
extern WSclient wsclient;

// 1. Инициализация контекста
void WSclient::initContext() {
    this->wsi = nullptr;
    this->running = 1;
    this->lastNnc = -1;
    this->resendTries = 0;
    this->isLineBusy = false;

    // Полная очистка очереди
    std::queue<MsgParser::PuhegUpperMessage> empty;
    std::swap(this->messageBuffer, empty);

    // Сброс текущего сообщения
    this->sentMessage = MsgParser::PuhegUpperMessage();

    this->lastBufferCheck = std::chrono::steady_clock::now();
    this->lastResendCheck = std::chrono::steady_clock::now();

    //std::cout << "[System] Внутренний контекст класса WSclient инициализирован." << std::endl;
}

// 2. Отправка квитанции 'S' (Success)
void WSclient::sendSuccess(struct lws *wsiParam) {
    std::vector<uint8_t> writeBuf(LWS_PRE + 1);
    writeBuf[LWS_PRE] = 'S';

    lws_write(wsiParam, &writeBuf[LWS_PRE], 1, LWS_WRITE_BINARY);
    //std::cout << "[QoS2] Отправлен байт 'S' (Success подтверждение для интерфейса)" << std::endl;
}

// 3. Получено подтверждение 'S' от устройства — сброс семафора
void WSclient::successReceived() {
    if (this->isLineBusy) {
        this->isLineBusy = false;
        this->sentMessage = MsgParser::PuhegUpperMessage();
        this->resendTries = 0;
        //std::cout << "[QoS2] Получено подтверждение 'S'. Линия свободна." << std::endl;
    }
}

// 4. Отправка MsgPack (единая точка выхода)
void WSclient::sendMessage(MsgParser::PuhegUpperMessage *pumsg, bool forceSend) {
    // Защитная проверка размера
    if (pumsg->packedMsg.size() <= LWS_PRE) {
        //std::cerr << "[QoS2] ОШИБКА: packedMsg пуст или слишком мал!" << std::endl;
        Log::error("Wsclient", "packedMsg пуст или слишком мал");
        return;
    }

    size_t size = pumsg->packedMsg.size() - LWS_PRE;

    // ЕДИНАЯ ПРОВЕРКА: линия QoS2 занята ИЛИ нет сети ИЛИ устройство выполняет процесс
    bool isDeviceBusy = MsgParser::isDeviceBusy();

    if (!forceSend && (this->isLineBusy || this->wsi == nullptr || isDeviceBusy)) {
        //std::cout << "[Wsclient] Линия занята, нет сети или устройство выполняет процесс. Пакет сохранен в буфер FIFO." << std::endl;
        Log::info("Wsclient", "Линия занята, нет сети или устройство выполняет процесс. Пакет сохранен в буфер FIFO.");
        this->messageBuffer.push(*pumsg);
        return;
    }

    // Если дошли сюда, регистрируем новую команду как активный процесс
    if (!isDeviceBusy) {
        MsgParser::startProcess(pumsg->thread);
    }


    // Реальная отправка в сокет
    lws_write(this->wsi, &pumsg->packedMsg[LWS_PRE], size, LWS_WRITE_BINARY);
    //std::cout << "[Wsclient] Упакованный MsgPack-пакет успешно отправлен в сеть." << std::endl;

    Log::info("Wsclient", "Сообщение отправлено в станцию");

    // Запоминаем состояние
    this->sentMessage = *pumsg;
    this->isLineBusy = true;

    // Сбрасываем счетчик только при свежей отправке
    if (!forceSend) {
        this->resendTries = 0;
    }
}

// 5. Обработка таймеров
void WSclient::processTimers() {
    auto currentTime = std::chrono::steady_clock::now();

    // 5.1. Проверка буфера FIFO (раз в 500 мс)
    auto passedBufferMs = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - this->lastBufferCheck).count();
    if (passedBufferMs >= 500) {
        this->lastBufferCheck = currentTime;

        if (!this->isLineBusy && this->wsi != nullptr && !this->messageBuffer.empty()) {
            MsgParser::PuhegUpperMessage nextMsg = this->messageBuffer.front();
            this->messageBuffer.pop();

            sendMessage(&nextMsg, false);
            //std::cout << "[QoS2] Сообщение извлечено из буфера и передано в sendMessage()." << std::endl;
        }
    }

    // 5.2. Ресенд (раз в 1000 мс)
    auto passedResendMs = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - this->lastResendCheck).count();
    if (passedResendMs >= 1000) {
        this->lastResendCheck = currentTime;

        if (this->isLineBusy && this->wsi != nullptr && this->sentMessage.id != 0) {
            if (this->resendTries < 3) {
                this->resendTries++;
                //std::cout << "[QoS2] Переотправка пакета (Попытка "
                //          << this->resendTries << "/3)" << std::endl;

                sendMessage(&this->sentMessage, true);
            } else {
                //std::cout << "[QoS2] Превышено число попыток. Пакет стерт из памяти." << std::endl;
                Log::error("Wsclient", "Превышено число попыток. Пакет стерт из памяти.");

                this->isLineBusy = false;
                this->sentMessage = MsgParser::PuhegUpperMessage();
                this->resendTries = 0;
            }
        }
    }
}

// 6. Callback для libwebsockets
int WSclient::callbackQos2Client(struct lws *wsi, enum lws_callback_reasons reason,
                                 void *user, void *in, size_t len) {

    switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        //std::cout << "[WebSocket] Подключение установлено!" << std::endl;
        Log::info("Wsclient", "Подключение установлено");
        wsclient.wsi = wsi;
        wsclient.isLineBusy = false;
        break;

    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        //std::cout << "[WebSocket] Ошибка подключения!" << std::endl;
        Log::error("Wsclient", "Ошибка подключения");
        wsclient.wsi = nullptr;
        wsclient.isLineBusy = false;
        break;

    case LWS_CALLBACK_CLIENT_CLOSED:
        //std::cout << "[WebSocket] Соединение закрыто." << std::endl;
        Log::info("Wsclient", "Соединение закрыто");
        wsclient.wsi = nullptr;
        wsclient.isLineBusy = false;
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE:
        if (len == 0 || in == nullptr) {
            break; // Пустой пакет, игнорируем
        }

        // 1. Проверяем, не байт ли это 'S' (Success) от устройства
        if (len == 1 && *(char*)in == 'S') {
            wsclient.successReceived();
        }
        // 2. Если это не 'S', значит пришли полезные данные (MsgPack)
        else {
            //std::cout << "[QoS2] Получены новые данные от интерфейса." << std::endl;
            //Log::info("Wsclient", "Новое сообщение Puheg");

            // ШАГ А: Мы ОБЯЗАНЫ отправить устройству байт 'S' (QoS2 ACK)
            wsclient.sendSuccess(wsi);

            // ШАГ Б: Десериализуем сырые байты в msgpack::object (как в твоем extractNnc)
            try {
                msgpack::object_handle oh = msgpack::unpack(reinterpret_cast<const char*>(in), len);
                msgpack::object deserialized = oh.get();

                // ШАГ В: Передаем готовый объект в твой СУЩЕСТВУЮЩИЙ метод
                MsgParser::dispatchIncomingPacket(deserialized);

            } catch (const std::exception& e) {
                //std::cerr << "[QoS2] Ошибка десериализации MsgPack: " << e.what() << std::endl;
                Log::error("Wsclient", "Ошибка десериализации MsgPack");
            }
        }
        break;

    case LWS_CALLBACK_CLIENT_WRITEABLE:
        // Сокет готов к записи (пока не используется, отправка идет напрямую)
        break;

    default:
        break;
    }

    // КРИТИЧЕСКИ ВАЖНО: возвращаем 0, чтобы libwebsockets продолжил работу
    return 0;
}

