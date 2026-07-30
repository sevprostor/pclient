#include "wsclient.h"

#include <iostream>

//MsgParser parser;

// 1. Инициализация контекста внутри класса
void WSclient::initContext() {
    this->wsi = nullptr;
    this->running = 1;
    this->last_nnc = -1;
    this->resend_trys = 0;
    this->sent_message.clear(); // Вектор C++ очищается одной командой

    // Очистка std::queue очереди FIFO
    std::queue<BufferedMessage> empty;
    std::swap(this->message_buffer, empty);

    //this->last_buffer_check = std::clock();
    //this->last_resend_check = std::clock();
    this->last_buffer_check = std::chrono::steady_clock::now();
    this->last_resend_check = std::chrono::steady_clock::now();
    std::cout << "[System] Внутренний контекст класса WSclient инициализирован." << std::endl;
}

// 2. Отправка квитанции успеха 'S' (83 ASCII)
void WSclient::sendSuccess(struct lws *wsi_param) {
    // Вектор C++ выделяет память автоматически. Нам нужен LWS_PRE оверхед + 1 байт
    std::vector<uint8_t> write_buf(LWS_PRE + 1);
    write_buf[LWS_PRE] = 'S';

    lws_write(wsi_param, &write_buf[LWS_PRE], 1, LWS_WRITE_BINARY);
    std::cout << "[QoS2] Отправлен байт 'S' (Подтверждение для интерфейса)" << std::endl;
}



// 5. Отправка и упаковка MsgPack (Аналог send_message из Python)
//void WSclient::sendMessage(const std::vector<uint8_t>& payload) {
void WSclient::sendMessage(MsgParser::PuhegUpperMessage *pumsg) {
    // Вычисляем чистый размер MsgPack данных для сети
    size_t size = pumsg->packedMsg.size() - LWS_PRE;

    // Формируем сообщение для очереди FIFO
    BufferedMessage msg;
    msg.payload = pumsg->packedMsg;

    // Логика QoS 2: проверяем состояние линии и сокета
    if (this->wsi == nullptr || !this->sent_message.empty()) {
        std::cout << "[QoS2] Линия занята или нет сети. Пакет сохранен в буфер FIFO." << std::endl;
        this->message_buffer.push(msg);
    } else {
        // Отправляем напрямую в сокет libwebsockets со сдвигом на LWS_PRE
        lws_write(this->wsi, &msg.payload[LWS_PRE], size, LWS_WRITE_BINARY);
        std::cout << "[QoS2] Упакованный MsgPack-пакет успешно отправлен в сеть." << std::endl;

        // Сохраняем копию вектора для ожидания подтверждения 'S'
        this->sent_message = msg.payload;
        this->resend_trys = 0;
    }
}


// 6. Обработка таймеров (Аналог асинхронных циклов Python)
void WSclient::processTimers() {
    // Берем текущую точку реального времени высокого разрешения
    auto current_time = std::chrono::steady_clock::now();

    // 1. Проверка буфера FIFO (Раз в 500 мс)
    auto passed_buffer_ms = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - this->last_buffer_check).count();
    if (passed_buffer_ms >= BUFFER_CHECK_INTERVAL_MS) {
        this->last_buffer_check = current_time;
        if (!this->message_buffer.empty() && this->wsi != nullptr && this->sent_message.empty()) {
            BufferedMessage msg = this->message_buffer.front();
            this->message_buffer.pop();

            size_t size = msg.payload.size() - LWS_PRE;
            lws_write(this->wsi, &msg.payload[LWS_PRE], size, LWS_WRITE_BINARY);
            std::cout << "[QoS2] Сообщение извлечено из буфера и отправлено." << std::endl;

            this->sent_message = msg.payload;
            this->resend_trys = 0;
        }
    }

    // 2. Проверка недоставленных сообщений и ресенд (Раз в 1000 мс)
    auto passed_resend_ms = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - this->last_resend_check).count();
    if (passed_resend_ms >= RESEND_CHECK_INTERVAL_MS) {
        this->last_resend_check = current_time;
        if (!this->sent_message.empty() && this->wsi != nullptr) {
            if (this->resend_trys < MAX_RESEND_TRIES) {
                size_t size = this->sent_message.size() - LWS_PRE;
                lws_write(this->wsi, &this->sent_message[LWS_PRE], size, LWS_WRITE_BINARY);
                this->resend_trys++;
                std::cout << "[QoS2] Переотправка пакета (Попытка " << this->resend_trys << "/" << MAX_RESEND_TRIES << ")" << std::endl;
            } else {
                std::cout << "[QoS2] Превышено число попыток. Пакет стерт из памяти." << std::endl;
                this->sent_message.clear();
                this->resend_trys = 0;
            }
        }
    }
}

// 7. Статический метод-трамплин для связи Си и C++
int WSclient::callbackQos2Client(struct lws *wsi, enum lws_callback_reasons reason,
                                 void *user, void *in, size_t len)
{
    struct lws_context *lws_ctx = lws_get_context(wsi);
    WSclient *obj = static_cast<WSclient*>(lws_context_user(lws_ctx));

    if (obj) {
        return obj->handleCallback(wsi, reason, user, in, len);
    }
    return 0;
}

// 8. Основной метод обработки сетевых событий класса
int WSclient::handleCallback(struct lws *wsi_param, enum lws_callback_reasons reason,
                             void *user, void *in, size_t len)
{
    switch (reason) {

    /*
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        std::cout << "[LWS] Успешное WebSocket подключение к устройству!" << std::endl;
        this->wsi = wsi_param;
        break;
    */

    case LWS_CALLBACK_CLIENT_ESTABLISHED:
    {
        std::cout << "[Wsclient] Успешное WebSocket подключение к устройству!" << std::endl;
        this->wsi = wsi_param;

        MsgParser::PuhegUpperMessage pumsg;
        pumsg.what = "initclient";

        // === АВТОМАТИЧЕСКАЯ ОТПРАВКА КОМАНДЫ ИНИЦИАЛИЗАЦИИ ===
        std::cout << "[System] Отправка пакета инициализации клиента..." << std::endl;

        //std::vector<uint8_t> empty_msg;
        //std::vector<uint8_t> init_packet = MsgParser::packMessage(&pumsg);
        parser.packMessage(&pumsg);

        this->sendMessage(&pumsg);
        // ====================================================
        break;
    }


    case LWS_CALLBACK_CLIENT_RECEIVE:
    {
        // А. Проверяем одиночный байт 'S' (Подтверждение нашей отправки QoS2)
        if (len == 1 && (*(unsigned char *)in) == 'S') {
            if (!this->sent_message.empty()) {
                std::cout << "[QoS2] Получено подтверждение 'S'. Пакет доставлен." << std::endl;
                this->sent_message.clear();
                this->resend_trys = 0;
            }
            break;
        }

        // Б. Обработка входящего пакета данных (>1 байта)
        std::cout << "[QoS2] Получены новые данные от интерфейса." << std::endl;
        sendSuccess(wsi_param); // Мгновенно шлем 'S' в ответ по протоколу

        try {
            // Извлекаем nnc через наш C++ MsgParser
            int64_t nnc = MsgParser::extractNnc((uint8_t *)in, len);

            // Проверка дедупликации пакетов
            if (nnc == this->last_nnc && nnc != -1) {
                std::cout << "[QoS2] Обнаружен дубликат пакета (nnc=" << nnc << "). Игнорируем." << std::endl;
            } else {
                this->last_nnc = nnc;
                std::cout << "[QoS2] Пакет уникален. Принимаем и десериализуем." << std::endl;

                // 1. Безопасная C++ распаковка всей структуры из сырых байт
                msgpack::object_handle oh = msgpack::unpack((const char *)in, len);
                msgpack::object obj = oh.get();

                // 2. Отображаем сырой MsgPack в консоли (старый красивый JSON-подобный лог)
                //this->handleMessage(obj);

                // =================================================================
                // КРИТИЧЕСКИЙ ШАГ: ОТПРАВЛЯЕМ ОБЪЕКТ НА СИСТЕМНЫЙ РАЗБОР В ДИСПЕТЧЕР
                // =================================================================
                MsgParser::dispatchIncomingPacket(obj);
                // =================================================================
            }
        } catch (const std::exception& e) {
            std::cerr << "[LWS Error] Ошибка обработки MsgPack: " << e.what() << std::endl;
        }
        break;
    }


    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        std::cout << "[LWS] Ошибка подключения к WebSocket." << std::endl;
        this->wsi = nullptr;
        break;

    case LWS_CALLBACK_CLIENT_CLOSED:
        std::cout << "[LWS] WebSocket соединение закрыто." << std::endl;
        this->wsi = nullptr;
        break;

    default:
        break;
    }
    return 0;
}


