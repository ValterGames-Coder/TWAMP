#include "Server.h"
#include <iostream>
#include <csignal>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <memory>

std::unique_ptr<Server> server;
volatile sig_atomic_t shutdownRequested = 0;

void signalHandler(int signum) {
    std::cerr << "Received signal " << signum << ", requesting shutdown..." << std::endl;
    shutdownRequested = 1;
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
        
        umask(0);
        setsid();
        chdir("/");
        
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }
    
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGPIPE, SIG_IGN); // Ignore broken pipe signals
    
    try {
        server = std::make_unique<Server>("/usr/local/etc/twamp-server/twamp-server.conf");
        if (!server->start()) {
            std::cerr << "Failed to start TWAMP server" << std::endl;
            return EXIT_FAILURE;
        }
        
        if (!runAsDaemon) {
            std::cout << "TWAMP server running in foreground" << std::endl;
        }
        
        // Wait for shutdown signal
        while (!shutdownRequested) {
            usleep(100000); // 100ms sleep to be more responsive
        }
        
        if (!runAsDaemon) {
            std::cout << "Shutdown signal received, cleaning up..." << std::endl;
        }
        
        // Cleanup - server should already be stopped by signal handler
        server.reset();
        
        if (!runAsDaemon) {
            std::cout << "Cleanup complete, exiting..." << std::endl;
        }
        
        // Force exit to avoid any lingering issues
        _exit(EXIT_SUCCESS);
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}