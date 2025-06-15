// twamp_server.cpp
#include <iostream>
#include <cstring>
#include <thread>
#include <atomic>
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>

constexpr int CONTROL_PORT = 862;          // TCP port for TWAMP control (handshake)
constexpr int UDP_PORT = 20000;             // UDP port for TWAMP test packets (echo)
constexpr size_t TWAMP_MSG_SIZE = 512;      // Максимальный размер UDP пакета для TWAMP Echo

std::atomic<bool> running{true};
int tcp_sockfd = -1;
int udp_sockfd = -1;

void signal_handler(int signal) {
    running = false;
    if (tcp_sockfd >= 0) {
        shutdown(tcp_sockfd, SHUT_RDWR);
        close(tcp_sockfd);
        tcp_sockfd = -1;
    }
    if (udp_sockfd >= 0) {
        close(udp_sockfd);
        udp_sockfd = -1;
    }
}

ssize_t readN(int sock, void* buffer, size_t len) {
    size_t total = 0;
    char* buf = (char*)buffer;
    while (total < len) {
        ssize_t ret = read(sock, buf + total, len - total);
        if (ret <= 0) {
            if (ret == 0) return total;
            if (errno == EINTR) continue;
            return -1;
        }
        total += ret;
    }
    return total;
}

ssize_t writeN(int sock, const void* buffer, size_t len) {
    size_t total = 0;
    const char* buf = (const char*)buffer;
    while (total < len) {
        ssize_t ret = write(sock, buf + total, len - total);
        if (ret <= 0) {
            if (ret == 0) return total;
            if (errno == EINTR) continue;
            return -1;
        }
        total += ret;
    }
    return total;
}

void udp_echo_server() {
    udp_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sockfd < 0) {
        perror("UDP socket");
        return;
    }

    sockaddr_in udp_addr{};
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_addr.s_addr = INADDR_ANY;
    udp_addr.sin_port = htons(UDP_PORT);

    if (bind(udp_sockfd, (sockaddr*)&udp_addr, sizeof(udp_addr)) < 0) {
        perror("UDP bind");
        close(udp_sockfd);
        udp_sockfd = -1;
        return;
    }

    std::cout << "UDP echo server listening on port " << UDP_PORT << std::endl;

    char buffer[TWAMP_MSG_SIZE];
    while (running) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        ssize_t recv_len = recvfrom(udp_sockfd, buffer, sizeof(buffer), 0, (sockaddr*)&client_addr, &client_len);
        if (recv_len < 0) {
            if (errno == EINTR) continue;
            perror("UDP recvfrom");
            break;
        }

        // Просто отправляем назад тот же пакет
        ssize_t sent_len = sendto(udp_sockfd, buffer, recv_len, 0, (sockaddr*)&client_addr, client_len);
        if (sent_len < 0) {
            perror("UDP sendto");
            break;
        }
    }

    if (udp_sockfd >= 0) {
        close(udp_sockfd);
        udp_sockfd = -1;
    }
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Запускаем UDP echo сервер в отдельном потоке
    std::thread udp_thread(udp_echo_server);

    tcp_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_sockfd < 0) {
        perror("socket");
        running = false;
        udp_thread.join();
        return 1;
    }

    int opt = 1;
    setsockopt(tcp_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(CONTROL_PORT);

    if (bind(tcp_sockfd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(tcp_sockfd);
        running = false;
        udp_thread.join();
        return 1;
    }

    if (listen(tcp_sockfd, 5) < 0) {
        perror("listen");
        close(tcp_sockfd);
        running = false;
        udp_thread.join();
        return 1;
    }

    std::cout << "TWAMP server TCP listening on port " << CONTROL_PORT << std::endl;

    while (running) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(tcp_sockfd, (sockaddr*)&client_addr, &client_len);
        if (client_sock < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        std::cout << "New TWAMP TCP connection from " << inet_ntoa(client_addr.sin_addr) << std::endl;

        unsigned char clientGreeting[64]{};
        unsigned char serverGreeting[64]{};
        unsigned char clientSetupResponse[64]{};
        unsigned char serverAcceptSession[64]{};

        // 1. Получить Greeting клиента
        if (readN(client_sock, clientGreeting, 64) != 64) {
            std::cerr << "Failed to receive client Greeting\n";
            close(client_sock);
            continue;
        }
        std::cout << "Received client Greeting\n";

        // 2. Отправить Greeting сервера
        memset(serverGreeting, 0, sizeof(serverGreeting));
        serverGreeting[0] = 1;  // version = 1
        if (writeN(client_sock, serverGreeting, 64) != 64) {
            std::cerr << "Failed to send server Greeting\n";
            close(client_sock);
            continue;
        }
        std::cout << "Sent server Greeting\n";

        // 3. Получить Setup Response от клиента
        if (readN(client_sock, clientSetupResponse, 64) != 64) {
            std::cerr << "Failed to receive Setup Response\n";
            close(client_sock);
            continue;
        }
        std::cout << "Received Setup Response\n";

        // 4. Отправить Accept Session
        memset(serverAcceptSession, 0, sizeof(serverAcceptSession));
        serverAcceptSession[0] = 1;  // версия
        if (writeN(client_sock, serverAcceptSession, 64) != 64) {
            std::cerr << "Failed to send Accept Session\n";
            close(client_sock);
            continue;
        }
        std::cout << "Sent Accept Session\n";

        // После handshake TCP соединение остаётся открытым,
        // сервер не реализует дальше управление сессией для простоты.
        // Можно тут добавить логику взаимодействия, сейчас просто ждём закрытия клиентом

        close(client_sock);
        std::cout << "Closed client connection\n";
    }

    if (tcp_sockfd >= 0) {
        close(tcp_sockfd);
        tcp_sockfd = -1;
    }

    running = false;
    if (udp_thread.joinable())
        udp_thread.join();

    std::cout << "Server shut down." << std::endl;
    return 0;
}

