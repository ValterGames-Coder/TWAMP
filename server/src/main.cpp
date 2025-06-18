#include "Server.h"
#include <iostream>
#include <csignal>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

std::unique_ptr<Server> server;

void signalHandler(int signum) {
    if (server) {
        server->stop();
    }
}

int main(int argc, char* argv[]) {
    bool runAsDaemon = true;
    
    if (argc > 1 && std::string(argv[1]) == "--foreground") {
        runAsDaemon = false;
    }
    
    if (runAsDaemon) {
        pid_t pid = fork();
        if (pid < 0) exit(EXIT_FAILURE);
        if (pid > 0) exit(EXIT_SUCCESS);
        
        umask(0);  // Теперь будет работать
        setsid();
        chdir("/");
        
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }
    
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    try {
        server = std::make_unique<Server>("/usr/local/etc/twamp-server/twamp-server.conf");
        if (!server->start()) {
            std::cerr << "Failed to start TWAMP server" << std::endl;
            return EXIT_FAILURE;
        }
        
        if (!runAsDaemon) {
            std::cout << "TWAMP server running in foreground" << std::endl;
        }
        
        while (true) {
            pause();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}