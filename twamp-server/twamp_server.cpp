// twamp_server_full.cpp

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <csignal>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <thread>
#include <atomic>

constexpr int CONTROL_PORT = 862;
constexpr int UDP_PORT = 862; // UDP для теста — совпадает с TCP портом по RFC
constexpr int UDP_TEST_PORT = 862 + 1; // Порт UDP для TWAMP test packets

std::atomic<bool> running(true);

void signal_handler(int signum) {
    running = false;
}

#pragma pack(push, 1)
struct ServerGreeting {
    uint32_t modes;
    uint8_t challenge[16];
    uint8_t salt[16];
    uint32_t count;
};

struct UDPTestPacket {
    uint8_t type;
    uint8_t reserved[11];
    uint64_t timestamp; // 64 бит, например
    // ... для примера, не строго RFC
};
#pragma pack(pop)

void udp_reflector_thread() {
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        perror("UDP socket");
        return;
    }

    sockaddr_in udp_addr{};
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_addr.s_addr = INADDR_ANY;
    udp_addr.sin_port = htons(UDP_TEST_PORT);

    if (bind(udp_sock, (sockaddr*)&udp_addr, sizeof(udp_addr)) < 0) {
        perror("UDP bind");
        close(udp_sock);
        return;
    }

    std::cout << "UDP reflector listening on port " << UDP_TEST_PORT << std::endl;

    while (running) {
        uint8_t buffer[1500];
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        ssize_t len = recvfrom(udp_sock, buffer, sizeof(buffer), 0,
                               (sockaddr*)&client_addr, &client_len);
        if (len < 0) {
            if (running) perror("UDP recvfrom");
            continue;
        }

        // Просто эхо: отправляем обратно тот же пакет
        ssize_t sent = sendto(udp_sock, buffer, len, 0,
                              (sockaddr*)&client_addr, client_len);
        if (sent != len) {
            perror("UDP sendto");
        }
    }

    close(udp_sock);
}

// Основная сессия TCP управления
void control_session(int client_sock, sockaddr_in client_addr) {
    std::cout << "New TWAMP control session from " << inet_ntoa(client_addr.sin_addr) << std::endl;

    // Отправляем Server Greeting (36 байт)
    ServerGreeting greeting{};
    greeting.modes = htonl(1); // Mode 1 - unauthenticated
    memset(greeting.challenge, 0, sizeof(greeting.challenge));
    memset(greeting.salt, 0, sizeof(greeting.salt));
    greeting.count = htonl(0);

    if (send(client_sock, &greeting, sizeof(greeting), 0) != sizeof(greeting)) {
        std::cerr << "Failed to send Server Greeting" << std::endl;
        close(client_sock);
        return;
    }

    // Принимаем Setup Response (48 байт)
    uint8_t setup_response[48]{};
    ssize_t recvd = recv(client_sock, setup_response, sizeof(setup_response), MSG_WAITALL);
    if (recvd != sizeof(setup_response)) {
        std::cerr << "Failed to receive Setup Response" << std::endl;
        close(client_sock);
        return;
    }

    // Отправляем Accept Session (84 байта)
    uint8_t accept_session[84]{};
    accept_session[0] = 0; // Message Type = Accept Session
    if (send(client_sock, accept_session, sizeof(accept_session), 0) != sizeof(accept_session)) {
        std::cerr << "Failed to send Accept Session" << std::endl;
        close(client_sock);
        return;
    }

    // Ждем Start Sessions Request (44 байта)
    uint8_t start_sessions[44]{};
    recvd = recv(client_sock, start_sessions, sizeof(start_sessions), MSG_WAITALL);
    if (recvd != sizeof(start_sessions)) {
        std::cerr << "Failed to receive Start Sessions" << std::endl;
        close(client_sock);
        return;
    }

    // Отвечаем Start Sessions Reply (20 байт)
    uint8_t start_sessions_reply[20]{};
    start_sessions_reply[0] = 0; // Message Type = Start Sessions Reply
    // остальные поля нули

    if (send(client_sock, start_sessions_reply, sizeof(start_sessions_reply), 0) != sizeof(start_sessions_reply)) {
        std::cerr << "Failed to send Start Sessions Reply" << std::endl;
        close(client_sock);
        return;
    }

    std::cout << "TWAMP session started with " << inet_ntoa(client_addr.sin_addr) << std::endl;

    // Теперь ждем Stop Sessions или просто закрываем по SIGINT
    while (running) {
        uint8_t buf[20]{};
        ssize_t r = recv(client_sock, buf, sizeof(buf), 0);
        if (r <= 0) break; // закрытие или ошибка
        // Для упрощения — игнорируем команды
    }

    std::cout << "TWAMP session ended with " << inet_ntoa(client_addr.sin_addr) << std::endl;
    close(client_sock);
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(CONTROL_PORT);

    if (bind(sockfd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(sockfd);
        return 1;
    }

    if (listen(sockfd, 5) < 0) {
        perror("listen");
        close(sockfd);
        return 1;
    }

    std::cout << "TWAMP server listening on TCP port " << CONTROL_PORT << std::endl;

    // Запускаем UDP отражатель в отдельном потоке
    std::thread udp_thread(udp_reflector_thread);

    while (running) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(sockfd, (sockaddr*)&client_addr, &client_len);
        if (client_sock < 0) {
            if (running) perror("accept");
            continue;
        }

        // Обрабатываем каждое соединение в отдельном потоке
        std::thread(control_session, client_sock, client_addr).detach();
    }

    std::cout << "Shutting down server..." << std::endl;
    close(sockfd);

    running = false;
    udp_thread.join();

    std::cout << "Server stopped." << std::endl;
    return 0;
}

