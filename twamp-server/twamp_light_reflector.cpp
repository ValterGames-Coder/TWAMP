#include <iostream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

constexpr int TWAMP_LIGHT_PORT = 862;
constexpr int BUFFER_SIZE = 1500;

int main() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(TWAMP_LIGHT_PORT);

    if (bind(sockfd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sockfd);
        return EXIT_FAILURE;
    }

    std::cout << "TWAMP Light reflector started on UDP port " << TWAMP_LIGHT_PORT << std::endl;

    char buffer[BUFFER_SIZE];
    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        ssize_t recv_len = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (sockaddr*)&client_addr, &client_len);
        if (recv_len < 0) {
            perror("recvfrom");
            continue;
        }

        // Здесь можно добавить обработку пакета, например, логирование или валидацию.

        ssize_t sent_len = sendto(sockfd, buffer, recv_len, 0, (sockaddr*)&client_addr, client_len);
        if (sent_len < 0) {
            perror("sendto");
            continue;
        }
    }

    close(sockfd);
    return 0;
}

