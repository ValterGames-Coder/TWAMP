#include "Daemon.hpp"

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fstream>
#include <cstdlib>
#include <string>

Daemon* Daemon::instance = nullptr;

Daemon::Daemon() : running(true) {}

void Daemon::start() 
{
    setup_signal_handler();
    daemonize();
    run();
}

void Daemon::signal_handler(int sig) 
{
    if (sig == SIGTERM && instance)
        instance->running = 0;
}

void Daemon::setup_signal_handler() 
{
    instance = this;
    signal(SIGTERM, signal_handler);
}

void Daemon::daemonize() 
{
    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS); // Родитель выходит

    if (setsid() < 0) exit(EXIT_FAILURE);

    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS); // Первый потомок выходит

    umask(0);
    chdir("/");

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

void Daemon::log(const std::string& message) 
{
    std::ofstream logFile("/tmp/daemon.log", std::ios::app);
    if (logFile.is_open()) 
    {
        logFile << message << std::endl;
    }
}

void Daemon::run() 
{
    while (running) 
    {
        log("Демон работает");
        sleep(5);
    }

    // Завершающие действия (если нужны)
}