#ifndef CONSOLE_H
#define CONSOLE_H

#pragma once
#include "wsclient.h"
#include "config.h"
#include <string>
#include <mutex>
#include <thread>


// Пространство имен или класс для изоляции логики терминала
class Console {
public:

    // Перерисовать промпт и текущий ввод
    static void redrawPrompt();

    //обрабатывать ввод
    void consoleInputThread(WSclient* client);
    std::string readLineWithRedraw();


};

#endif // CONSOLE_H
