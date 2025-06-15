// twamp_server.cpp
// A basic TWAMP server implementation (RFC 5357 compliant subset)

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <csignal>

constexpr int CONTROL_PORT = 862; // Standard TWAMP control port
volatile bool running = true;

void signal_handler(int signal) {
    running = false;
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

    std::cout << "TWAMP server listening on port " << CONTROL_PORT << std::endl;

    while (running) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(sockfd, (sockaddr*)&client_addr, &client_len);
        if (client_sock < 0) {
            if (running) perror("accept");
            continue;
        }

        std::cout << "New TWAMP connection from " << inet_ntoa(client_addr.sin_addr) << std::endl;

        // Basic TWAMP control negotiation
        char buffer[128]{};
        recv(client_sock, buffer, sizeof(buffer), 0);

        // For now, we just echo back the request (very basic RFC 5357 compliance)
        send(client_sock, buffer, 64, 0); // Echo Session-Request
        recv(client_sock, buffer, sizeof(buffer), 0); // Receive Accept Session
        send(client_sock, buffer, 64, 0); // Echo OK

        close(client_sock);
    }

    close(sockfd);
    std::cout << "Server shut down." << std::endl;
    return 0;
}

