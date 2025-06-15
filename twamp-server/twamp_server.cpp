#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <csignal>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

constexpr int UDP_PORT = 862; // TWAMP-Light UDP порт
volatile bool running = true;

void signal_handler(int) {
    running = false;
}

void daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    if (setsid() < 0) {
        perror("setsid");
        exit(EXIT_FAILURE);
    }

    signal(SIGHUP, SIG_IGN);

    pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    if (chdir("/") < 0) {
        perror("chdir");
        exit(EXIT_FAILURE);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    int fd = open("/dev/null", O_RDWR);
    if (fd < 0) {
        perror("open /dev/null");
        exit(EXIT_FAILURE);
    }
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > 2) close(fd);
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    daemonize();

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        exit(EXIT_FAILURE);
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(UDP_PORT);

    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        exit(EXIT_FAILURE);
    }

    while (running) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        uint8_t buffer[1500];

        ssize_t received = recvfrom(sock, buffer, sizeof(buffer), 0,
                                    (sockaddr*)&client_addr, &client_len);
        if (received < 0) {
            continue;
        }
        if (received < 4) {
            continue;
        }

        // Отражаем пакет без изменений
        sendto(sock, buffer, received, 0, (sockaddr*)&client_addr, client_len);
    }

    close(sock);

    return 0;
}
