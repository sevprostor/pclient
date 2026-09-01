#include "events.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>
#include <thread>
#include <chrono>

struct Options {
    std::string mode;                 // send | recv
    std::string file;
    std::string dest;                 // пока id/IP, позже — криптоадрес
    std::string outDir = ".";
    uint16_t port = 47000;
    bool verbose = false;
};

static void usage(const char* prog) {
    printf("pscp — передача файлов поверх Puheg mesh\n");
    printf("  %s send <file> <dest> [-v] [-P port]\n", prog);
    printf("  %s recv [--dir <path>] [-v] [-P port]\n", prog);
}

static bool parseArgs(int argc, char** argv, Options& o) {
    if (argc < 2) return false;
    o.mode = argv[1];
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-v") o.verbose = true;
        else if (a == "-P" && i + 1 < argc) o.port = (uint16_t)atoi(argv[++i]);
        else if (a == "--dir" && i + 1 < argc) o.outDir = argv[++i];
        else if (o.mode == "send" && o.file.empty()) o.file = a;
        else if (o.mode == "send" && o.dest.empty()) o.dest = a;
        else return false;
    }
    if (o.mode == "send") return !o.file.empty() && !o.dest.empty();
    if (o.mode == "recv") return true;
    return false;
}

int main(int argc, char** argv) {
    Options o;
    if (!parseArgs(argc, argv, o)) { usage(argv[0]); return 1; }

    EventBusClient events;
    if (o.verbose && events.start())
        printf("Подписан на события драйвера\n");

    if (o.mode == "send") {
        std::ifstream f(o.file, std::ios::binary);
        if (!f) { fprintf(stderr, "Не открыть %s\n", o.file.c_str()); return 1; }
        f.seekg(0, std::ios::end);
        size_t size = (size_t)f.tellg();
        printf("Отправка %s (%zu байт) -> %s, порт %u\n",
               o.file.c_str(), size, o.dest.c_str(), o.port);
        // TODO: Transport -> KEX -> Crypto -> Transfer::send

    } else {
        printf("Приём: слушаю UDP %u, пишу в %s\n", o.port, o.outDir.c_str());
        // TODO: Transfer::recv
        while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}
