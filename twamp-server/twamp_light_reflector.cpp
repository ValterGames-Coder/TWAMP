#include <iostream>
#include <iomanip>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <chrono>
#include <cstdint>

constexpr int TWAMP_LIGHT_PORT = 862;
constexpr int BUFFER_SIZE = 1500;

void set_ntp_timestamp(uint8_t* buffer) {
    using namespace std::chrono;

    auto now = system_clock::now();
    auto duration_since_epoch = now.time_since_epoch();

    // Сдвиг для эпохи NTP (с 1900), системное время считается от 1970
    uint64_t ntp_epoch_offset = 2208988800ULL;

    // Время в секундах и микросекундах
    auto sec_duration = duration_cast<seconds>(duration_since_epoch);
    auto microsec_duration = duration_cast<microseconds>(duration_since_epoch - sec_duration);

    uint32_t sec = static_cast<uint32_t>(sec_duration.count() + ntp_epoch_offset);
    uint32_t frac = static_cast<uint32_t>((uint64_t(microsec_duration.count()) * 0x100000000ULL) / 1000000ULL);

    buffer[0] = (sec >> 24) & 0xFF;
    buffer[1] = (sec >> 16) & 0xFF;
    buffer[2] = (sec >> 8) & 0xFF;
    buffer[3] = (sec) & 0xFF;

    buffer[4] = (frac >> 24) & 0xFF;
    buffer[5] = (frac >> 16) & 0xFF;
    buffer[6] = (frac >> 8) & 0xFF;
    buffer[7] = (frac) & 0xFF;
}

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

    uint8_t buffer[BUFFER_SIZE];

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        ssize_t recv_len = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (sockaddr*)&client_addr, &client_len);
        if (recv_len < 0) {
            perror("recvfrom");
            continue;
        }

        if (recv_len < 44) {
            std::cerr << "Received too small packet, ignoring\n";
            continue;
        }

        // Копируем sequence (4 байта) из пакета запроса в ответ
        // Sequence — первые 4 байта (offset 0..3) в пакете
        // Они должны остаться без изменений
        // Но можно проверить, например:
        uint32_t sequence = ntohl(*(uint32_t*)(buffer));
        // Не изменяем, просто для логов
        std::cout << "[TEST] Packet Received :: Sequence " << sequence << std::endl;

        // Устанавливаем TTL в 255 (байт с offset 7)
        buffer[7] = 255;

        // Записываем Receive Timestamp в offset 8 (8 байт)
        set_ntp_timestamp(buffer + 8);

        // Записываем Send Timestamp в offset 16 (8 байт)
        // Делать сразу перед отправкой
        set_ntp_timestamp(buffer + 16);

        // Отправляем обратно тот же пакет, с обновлёнными временными метками
        ssize_t sent_len = sendto(sockfd, buffer, recv_len, 0, (sockaddr*)&client_addr, client_len);
        if (sent_len < 0) {
            perror("sendto");
            continue;
        }

        std::cout << "[TEST] Packet Sent :: Sequence " << sequence << std::endl;
    }

    close(sockfd);
    return 0;
}

