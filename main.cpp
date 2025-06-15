// TWAMP Server (Control + Session-Reflector) по RFC 5357
// C++ реализация без внешних библиотек

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <ctime>
#include <csignal>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>

#define CONTROL_PORT 862
#define TEST_PORT_START 20000
#define BUFFER_SIZE 1500
#define NTP_UNIX_EPOCH_OFFSET 2208988800U

volatile bool keepRunning = true;

void signalHandler(int signum) {
    keepRunning = false;
}

uint64_t getNtpTimestamp() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    uint64_t seconds = tv.tv_sec + NTP_UNIX_EPOCH_OFFSET;
    uint64_t fraction = ((uint64_t)tv.tv_usec << 32) / 1000000;
    return (seconds << 32) | (fraction & 0xFFFFFFFF);
}

void daemonize() {
    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);
    umask(0);
    setsid();
    if (chdir("/") < 0) exit(EXIT_FAILURE);
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

int setupUdpSocket(uint16_t port) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) return -1;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        return -1;
    }

    return sockfd;
}

void sessionReflector(uint16_t port) {
    int sockfd = setupUdpSocket(port);
    if (sockfd < 0) return;

    char buffer[BUFFER_SIZE];
    struct sockaddr_in clientAddr{};
    socklen_t addrLen = sizeof(clientAddr);

    while (keepRunning) {
        ssize_t received = recvfrom(sockfd, buffer, BUFFER_SIZE, 0,
                                    (struct sockaddr *)&clientAddr, &addrLen);
        if (received > 0) {
            uint64_t recvTime = getNtpTimestamp();
            memcpy(buffer + 16, &recvTime, sizeof(recvTime));
            sendto(sockfd, buffer, received, 0,
                   (struct sockaddr *)&clientAddr, addrLen);
        }
    }

    close(sockfd);
}

void handleControlConnection(int clientSock) {
    char buffer[BUFFER_SIZE];
    recv(clientSock, buffer, sizeof(buffer), 0);

    // Simпle negotiation (dummy version)
    uint16_t port = TEST_PORT_START;
    buffer[0] = (port >> 8) & 0xFF;
    buffer[1] = port & 0xFF;
    send(clientSock, buffer, 2, 0);

    if (fork() == 0) {
        sessionReflector(port);
        exit(0);
    }
}

void controlServer() {
    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock < 0) exit(EXIT_FAILURE);

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(CONTROL_PORT);

    if (bind(serverSock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        exit(EXIT_FAILURE);

    listen(serverSock, 5);

    while (keepRunning) {
        struct sockaddr_in clientAddr{};
        socklen_t addrLen = sizeof(clientAddr);
        int clientSock = accept(serverSock, (struct sockaddr *)&clientAddr, &addrLen);
        if (clientSock >= 0) {
            if (fork() == 0) {
                close(serverSock);
                handleControlConnection(clientSock);
                close(clientSock);
                exit(0);
            } else {
                close(clientSock);
            }
        }
    }

    close(serverSock);
}

int main() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    daemonize();

    controlServer();

    return 0;
}

