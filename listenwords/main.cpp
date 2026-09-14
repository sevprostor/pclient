#include "config.h"
#include "transport.h"
#include "log.h"      // <-- единый вывод
#include "../libs/json.hpp"
#include "words.h"
#include "file.h"
#include "ebparser.h"
#include "ebparser.h"

#include "process.h"
#include <string>
#include <thread>
#include <fstream>
#include <random>

//File file;
Config config;

// Глобальное состояние для отслеживания процессов
// переделать на runningProc

RunningProcess runningProc;

//uint64_t g_currentThread = 0;
//std::string g_processState = "";
//std::chrono::steady_clock::time_point g_lastProcessComplete;
//bool g_processActive = false;

//Words words;
//File file;


using json = nlohmann::json;

static void usage(const char* prog) {
    Log::info("ListenWords", "listenwords — приём событий шины и PW-кадров");
    Log::info("ListenWords", "  ", prog, " [-eb port] [-config path]");
}

static std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        bytes.push_back(static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

int main(int argc, char** argv) {


    // Предварительный проход: ищем -config
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "-config" && i + 1 < argc)
            config.configPath = argv[i+1];
    }

    // Загружаем конфиг и применяем аргументы командной строки
    config.loadFromFile(config.configPath);
    if (!config.parseCommandLine(argc, argv)) {
        usage(argv[0]);
        return 1;
    }

    Transport transport;
    if (!transport.init(config.eventBusPort)) {
        Log::error("ListenWords", "Не удалось инициализировать Transport на порту ",
                   config.eventBusPort);
        return 1;
    }

    Log::info("ListenWords", "Слушаю EventBus на порту ", config.eventBusPort, "...");

        //Words words;
    File file;         // <-- НОВОЕ: объект для сборки файлов


    while (true) {
        EBMessage emsg = transport.poll(100);
        if (emsg.evenbus) EBParser::parseEmsg(emsg, file); //поправить название!


        // Если адресная книга загружена — сканируем outbox и отправляем файлы
        if (!config.addressbook.empty()) {
            // 1. По 1 самому старому файлу из каждого каталога контакта
            auto candidates = file.scanOutboxAll();

            // 2+3. Новый spool — только если в spool < 4 каталогов передач
            if (!candidates.empty() && file.countSpoolTransfers() < config.maxTransfers) {

                auto oldest = std::min_element(candidates.begin(), candidates.end(),
                                               [](const File::OutboxFile& a, const File::OutboxFile& b) {
                                                   return a.timeCreated < b.timeCreated;
                                               });
                //file.spoolFile(*oldest, 2000);
                file.spoolFile(*oldest, config.chunkSize);

            }

            // 4. Готовим .pwp — только если в spool нет активных пакетов.
            // ВАЖНО: шаг 4 живёт вне проверки candidates: передачи могут
            // доготавливаться, даже когда outbox уже пуст
            if (file.countPwpFiles() == 0) {
                file.prepareWaitingPackets(config.maxTransfers);
            }

            // Процессы теперь приходят адресно - чужой процесс сюда не придет
            // после отправки взять первый тред для отслеживания процесса

            // Если никакой процесс не идет, и со времени завершения предыдущего прошло
            // более (config.sendTXDelay=1sec + random(config.sendTXDelay/2)), то идет отправка.
            // 1. Просмотреть очередь. Пвп с ретраями > config.maxProcessRetries=2 - удалить
            // 1. Взять из очереди самый старый пвп и отправить, при этом получить номер треда
            // 2. Когда процесс с нашим тредом закончится ОК или ФЕЙЛ, запомнить время.
            //  - при неудаче переименовать файл, добавив ретрай. Чтобы он стал самым новым в папке
            //  - при успехе удалить пвп



            // 5. ОТПРАВКА: если процесс не активен и прошло достаточно времени
            //if (!g_processActive) {
            if(!runningProc.running && !config.driverState.busy){

                auto now = std::chrono::steady_clock::now();
                //auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_lastProcessComplete).count();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - runningProc.lastProcessCompleted).count();

                // Задержка: sendTXDelay + random(0..sendTXDelay/2)
                static std::random_device rd;
                static std::mt19937 gen(rd());
                int delay = config.sendTXDelay + (gen() % (config.sendTXDelay / 2 + 1));

                ////////////////////////////////////////////////////
                ///
                /// Начало рабочего такта отправки
                ///

                if (elapsed >= delay) {


                    // Удаляем .pwp с retries > maxProcessRetries
                    // Просматривается количество ретраев в имени файла "part-total-retries-file.name"
                    auto pwpFiles = file.getPwpFiles();
                    for (const auto& pwp : pwpFiles) {
                        if (pwp.retries > config.maxProcessRetries) {
                            Log::warn("ListenWords", "❌ .pwp превысил лимит ретраев, удаляем: ",
                                      pwp.path.string());
                            file.removePwp(pwp.path);
                        }
                    }

                    // Обновляем список после удаления
                    pwpFiles = file.getPwpFiles();

                    if (!pwpFiles.empty()) {
                        // Берём самый старый .pwp
                        const auto& oldest = pwpFiles.front();

                        // Читаем содержимое .pwp
                        std::ifstream in(oldest.path.string(), std::ios::binary);
                        if (in) {
                            std::vector<uint8_t> packet((std::istreambuf_iterator<char>(in)),
                                                        std::istreambuf_iterator<char>());
                            in.close();

                            // Отправляем
                            if (transport.sendFrame(oldest.destIp, packet)){

                                Log::info("ListenWords", "📤 Отправлен .pwp: ", oldest.path.filename().string(),
                                          " (part ", oldest.part + 1, "/", oldest.total,
                                          ", uuid=", oldest.uuid, ")");


                                //Начать новый процесс
                                //Запомнить, с каким файлом идет работа.
                                runningProc.processingUUID = oldest.uuid;
                                runningProc.running = true;
                                runningProc.state = "";



                                //Сразу приписать к имени новый ретрай
                                //В случае успеха надо будет просто удалить, иначе - прибавить еще ретрай
                                //file.incrementPwpRetry(oldest.path);

                            } else {
                                Log::error("ListenWords", "❌ Ошибка отправки .pwp");

                            }
                        }
                    }
                }
            } else {
                // Процесс активен — проверяем завершение
                //if (g_processState == "OK" || g_processState == "FAIL") {
                if (runningProc.state == "OK" || runningProc.state == "FAIL") {
                    //g_lastProcessComplete = std::chrono::steady_clock::now();
                    runningProc.lastProcessCompleted = std::chrono::steady_clock::now();
                    //g_processActive = false;
                    runningProc.running = false;

                    // Находим соответствующий .pwp
                    auto pwpFiles = file.getPwpFiles();
                    for (const auto& pwp : pwpFiles) {

                        Log::info("ListenWords", "смотрим pwp - ", pwp.uuid, " proc - ", runningProc.thread);


                        if (pwp.uuid == runningProc.processingUUID) {



                            //Если процесс завершился неудачей, то переименовать ретрай и больше ничего не делать
                            if (runningProc.state == "FAIL"){
                                file.incrementPwpRetry(pwp.path);
                            }

                            //В случае успеха - отрапортовать и удалить pwp
                            if (runningProc.state == "OK") {

                                Log::info("ListenWords", "✅ .pwp успешно отправлен: ",
                                          pwp.path.filename().string());

                                // === ПРОВЕРКА: это файл пруфа? ===
                                // Имя .pwp: <part>-<total>-<retries>-<chunkName>.pwp
                                // Отсекаем .pwp, смотрим расширение имени чанка
                                std::string pwpName = pwp.path.filename().string();
                                // Убираем расширение .pwp
                                size_t dotPwp = pwpName.rfind(".pwp");
                                std::string chunkName = (dotPwp != std::string::npos)
                                                            ? pwpName.substr(0, dotPwp)
                                                            : pwpName;

                                // Ищем последний дефис (перед именем чанка)
                                size_t lastDash = chunkName.rfind('-');
                                std::string originalFilename = (lastDash != std::string::npos)
                                                                   ? chunkName.substr(lastDash + 1)
                                                                   : chunkName;

                                // Получаем расширение оригинального файла (регистронезависимо)
                                std::string ext = fs::path(originalFilename).extension().string();
                                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                                bool isProofFile = (ext == ".pwproof");

                                if (isProofFile) {
                                    Log::info("ListenWords", "📋 Это файл пруфа, сразу в approved");

                                    // Сразу перемещаем чанок из waiting в approved
                                    //file.approveChunk(pwp.uuid, pwp.part);
                                    std::vector<uint8_t> parts;
                                    parts.emplace_back(pwp.part);
                                    //Делаем вид, будто получили пруф.
                                    file.processIncomingProof(pwp.uuid, parts);

                                    // Удаляем .pwp
                                    //file.removePwp(pwp.path);
                                } else {
                                    // Обычный файл — ждём пруф от получателя
                                    Log::info("ListenWords", "⏳ Ожидание пруфа от получателя");
                                }



                                Log::info("ListenWords", "✅ .pwp успешно отправлен, удаляем: ", pwp.path.filename().string());
                                file.removePwp(pwp.path);

                            }


                            //else {
                            //    Log::warn("ListenWords", "⚠️ .pwp не отправлен, увеличиваем retry: ",
                            //              pwp.path.filename().string());
                            //    file.incrementPwpRetry(pwp.path);
                            //}

                            break;
                        }
                    }

                    runningProc.thread = 0;
                    runningProc.state = "";
                }
            }
        }

        // Задержка между итерациями главного цикла
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }




        return 0;
    }
