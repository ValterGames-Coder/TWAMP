// TWAMP Light Session-Reflector (Server) — исправлено согласно RFC 5357
// Работает в режиме без аутентификации и без шифрования

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <chrono>

using namespace std;
using namespace std::chrono;

#pragma pack(push, 1)
struct TwampTestRequest {
    uint32_t sequence_number;
    uint64_t timestamp;
    uint32_t error_estimate;
    uint32_t mbz;
};

struct TwampTestResponse {
    uint32_t sequence_number;           // новое значение, независимое от sender
    uint64_t timestamp;                 // время отправки ответа
    uint32_t error_estimate;
    uint32_t mbz1;
    uint64_t receive_timestamp;         // время получения запроса
    uint32_t mbz2;
    uint32_t sender_sequence_number;
    uint32_t mbz3;
    uint64_t sender_timestamp;
    uint32_t sender_error_estimate;
    uint32_t mbz4;
    uint8_t sender_ttl;
    uint8_t mbz5[15];                   // MBZ padding
    uint8_t hmac[16];                   // игнорируем в режиме без HMAC
    uint8_t padding[8];                // минимальное заполнение, можно увеличить
};
#pragma pack(pop)

uint64_t getNtpTimestamp() {
    using namespace chrono; 
    auto now = system_clock::now();
    auto duration = now.time_since_epoch();
    uint64_t seconds = duration_cast<seconds>(duration).count();
    uint64_t fraction = ((duration_cast<nanoseconds>(duration).count() % 1000000000ULL) << 32) / 1000000000ULL;
    return ((seconds + 2208988800ULL) << 32) | fraction; // NTP timestamp
}

int main() {
    const int port = 862;
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in server_addr{}, client_addr{};
    socklen_t addr_len = sizeof(client_addr);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        return 1;
    }

    cout << "TWAMP Light Server (Reflector) запущен на порту " << port << endl;

    while (true) {
        TwampTestRequest request{};
        ssize_t recv_len = recvfrom(sockfd, &request, sizeof(request), 0, (sockaddr*)&client_addr, &addr_len);
        if (recv_len != sizeof(request)) {
            cerr << "Получен некорректный размер пакета: " << recv_len << endl;
            continue;
        }

        uint64_t recv_time = getNtpTimestamp();

        TwampTestResponse response{};
        response.sequence_number = htonl(ntohl(request.sequence_number)); // новый sequence, можно инкрементировать
        response.timestamp = getNtpTimestamp();
        response.error_estimate = htonl(0); // пока без оценки
        response.receive_timestamp = recv_time;
        response.sender_sequence_number = request.sequence_number;
        response.sender_timestamp = request.timestamp;
        response.sender_error_estimate = request.error_estimate;
        response.sender_ttl = 255; // hardcoded TTL, можно считать через IP_RECVTTL
        memset(response.padding, 0, sizeof(response.padding));

        sendto(sockfd, &response, sizeof(response), 0, (sockaddr*)&client_addr, addr_len);
    }

    close(sockfd);
    return 0;
}

