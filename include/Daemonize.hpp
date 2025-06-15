#pragma once

class Daemonize {
public:
    // Переводит текущий процесс в демон и при необходимости записывает PID-файл
    static void run(const char* pidFile = nullptr);
};