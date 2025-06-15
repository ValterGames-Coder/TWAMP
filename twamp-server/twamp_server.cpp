// twamp_server.cpp - TWAMP Light Session Reflector (UDP echo daemon)

#include <iostream>
#include <fstream>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>

constexpr int UDP_PORT = 20000; // TWAMP-Test port range
constexpr const char* PID_PATH = "/run/twamp_server.pid";
volatile bool running = true;

void handle_signal(int) {
    running = false;
}

void daemonize() {
    if (fork() > 0) exit(0);  // Parent exits
    setsid();                 // Create new session
    if (fork() > 0) exit(0);  // First child exits

    umask(0);
    chdir("/");

    // Redirect std* to /dev/null
    int fd = open("/dev/null", O_RDWR);
    if (fd != -1) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > 2) close(fd);
    }
}

int main() {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    daemonize();

    // Write PID file
    std::ofstream pidf(PID_PATH);
    if (!pidf) return 1;
    pidf << getpid() << std::endl;
    pidf.close();

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) return 1;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(UDP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        return 1;
    }

    char buffer[1500];
    sockaddr_in client{};
    socklen_t client_len = sizeof(client);

    while (running) {
        ssize_t len = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                               (sockaddr*)&client, &client_len);
        if (len > 0) {
            sendto(sockfd, buffer, len, 0, (sockaddr*)&client, client_len);
        }
    }

    unlink(PID_PATH);
    close(sockfd);
    return 0;
}

