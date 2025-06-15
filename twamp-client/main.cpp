#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

struct TWAMPServerGreeting {
    uint32_t unused[3];
    uint32_t modes;
    uint32_t challenge[4];
    uint32_t salt[4];
    uint32_t count;
    uint32_t mbz[3];
} __attribute__((packed));

struct TWAMPSetupResponse {
    uint32_t mode;
    uint32_t keyid[20];
    uint32_t token[16];
    uint32_t client_iv[4];
} __attribute__((packed));

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <server_ip>" << std::endl;
        return 1;
    }
    
    std::string server_ip = argv[1];
    
    // Создать сокет
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
        return 1;
    }
    
    // Настроить адрес сервера
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(862);
    
    if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "Invalid server IP address" << std::endl;
        close(sock);
        return 1;
    }
    
    // Подключиться к серверу
    std::cout << "Connecting to TWAMP server " << server_ip << ":862..." << std::endl;
    
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Failed to connect to server: " << strerror(errno) << std::endl;
        close(sock);
        return 1;
    }
    
    std::cout << "Connected successfully!" << std::endl;
    
    // Получить Server Greeting
    TWAMPServerGreeting greeting;
    ssize_t bytes = recv(sock, &greeting, sizeof(greeting), 0);
    if (bytes != sizeof(greeting)) {
        std::cerr << "Failed to receive server greeting (got " << bytes << " bytes)" << std::endl;
        close(sock);
        return 1;
    }
    
    std::cout << "Received Server Greeting:" << std::endl;
    std::cout << "  Modes: 0x" << std::hex << ntohl(greeting.modes) << std::dec << std::endl;
    
    // Отправить Setup Response
    TWAMPSetupResponse setup;
    memset(&setup, 0, sizeof(setup));
    setup.mode = 0; // Unauthenticated mode
    
    std::cout << "Sending Setup Response..." << std::endl;
    if (send(sock, &setup, sizeof(setup), 0) < 0) {
        std::cerr << "Failed to send setup response: " << strerror(errno) << std::endl;
        close(sock);
        return 1;
    }
    
    // Получить Server Start
    uint32_t server_start;
    bytes = recv(sock, &server_start, sizeof(server_start), 0);
    if (bytes != sizeof(server_start)) {
        std::cerr << "Failed to receive server start" << std::endl;
        close(sock);
        return 1;
    }
    
    uint32_t start_value = ntohl(server_start);
    std::cout << "Received Server Start: " << start_value;
    
    if (start_value == 0) {
        std::cout << " (ACCEPT)" << std::endl;
        std::cout << "✓ TWAMP Control Connection established successfully!" << std::endl;
        std::cout << "✓ Server is working correctly!" << std::endl;
    } else {
        std::cout << " (REJECT)" << std::endl;
        std::cout << "✗ Server rejected the connection" << std::endl;
    }
    
    close(sock);
    return 0;
}
