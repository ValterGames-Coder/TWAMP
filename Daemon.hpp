#pragma once

#include <csignal>

class Daemon
{
public:
    Daemon();
    void start();

private:
    volatile sig_atomic_t running; // не кэширует значение в регист
    static Daemon* instance;

    static void signal_handler(int sig);
    void setup_signal_handler();
    void daemonize();
    void log(const std::string& message);
    void run();
};