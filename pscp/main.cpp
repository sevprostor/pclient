#include "config.h"
#include "events.h"
#include "transport.h"
#include "hauler.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <atomic>

std::atomic<bool> g_running{true};

struct Options {
    std::string mode;
    std::string file;
    std::string dest;
    std::string outDir = ".";
    bool verbose = false;
};

static void usage(const char* prog) {
    printf("pscp — передача файлов поверх Puheg mesh\n");
    printf("  %s send <file> <dest_ip> [-v] [-eb port] [-config path]\n", prog);
    printf("  %s recv [--dir <path>] [-v] [-eb port] [-config path]\n", prog);
}

static bool parseArgs(int argc, char** argv, Options& o) {
    if (argc < 2) return false;
    o.mode = argv[1];

    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-v") {
            o.verbose = true;
        } else if (a == "--dir" && i + 1 < argc) {
            o.outDir = argv[++i];
        } else if (a == "-config" || a == "-eb") {
            i++;
        } else if (o.mode == "send" && o.file.empty()) {
            o.file = a;
        } else if (o.mode == "send" && o.dest.empty()) {
            o.dest = a;
        } else {
            return false;
        }
    }

    if (o.mode == "send") return !o.file.empty() && !o.dest.empty();
    if (o.mode == "recv") return true;
    return false;
}

int main(int argc, char** argv) {
    Options opts;

    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "-config" && i + 1 < argc) {
            config.configPath = argv[i+1];
        }
    }

    config.loadFromFile(config.configPath);

    if (!config.parseCommandLine(argc, argv) || !parseArgs(argc, argv, opts)) {
        usage(argv[0]);
        return 1;
    }

    if (opts.verbose) {
        printf("[Config] EventBus port: %u (from %s)\n", config.eventBusPort, config.configPath.c_str());
    }

    EventBusClient eventBus;
    Transport transport;
    Hauler hauler(transport);

    if (!eventBus.start(config.eventBusPort)) {
        fprintf(stderr, "Не удалось подключиться к EventBus на порту %u\n", config.eventBusPort);
        return 1;
    }

    if (!transport.init(config.eventBusPort)) {
        fprintf(stderr, "Не удалось инициализировать Transport на порту %u\n", config.eventBusPort);
        return 1;
    }

    if (opts.verbose) {
        //Вывести то, что свалилось в сокет для нас
        eventBus.setOnEvent([](const std::string& event) {
            printf("[EventBus] %s\n", event.c_str());
        });
    }

    //в каком режиме работаем? Отправка или висеть на приеме
    if (opts.mode == "send") {
        if (!hauler.sendFile(opts.file, opts.dest)) {
            fprintf(stderr, "Ошибка передачи файла\n");
        }
    } else {
        hauler.recvFiles(opts.outDir);
    }

    hauler.stop();
    transport.stop();
    eventBus.stop();

    printf("[System] Работа программы завершена.\n");
    return 0;
}
