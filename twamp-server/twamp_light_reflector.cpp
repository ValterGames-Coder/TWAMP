#include <iostream>
#include <cstring>
#include <csignal>
#include <chrono>
#include <netinet/in.h>
#include <unistd.h>

int sockfd;
bool running = true;

struct NtpTimestamp {
    uint32_t seconds;
    uint32_t fraction;
};

NtpTimestamp get_ntp_time() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto duration = now.time_since_epoch();

    uint64_t ms = duration_cast<milliseconds>(duration).count();
    uint32_t sec = ms / 1000;
    uint32_t frac = (uint32_t)(((ms % 1000) / 1000.0) * (1LL << 32));

    const uint32_t NTP_UNIX_OFFSET = 2208988800U;
    return { sec + NTP_UNIX_OFFSET, frac };
}

void handle_signal(int) {
    running = false;
    close(sockfd);
}

int main() {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(20000);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    std::cout << "TWAMP Light RFC-compliant reflector running on port 20000\n";

    while (running) {
        char buffer[1500];
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        ssize_t recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                                    (sockaddr*)&client_addr, &client_len);
        if (recv_len < 32) continue;

        // Формируем ответ
        char reply[1500];
        memset(reply, 0, sizeof(reply));

        // MBZ (0), Control (0)
        reply[0] = 0;
        reply[1] = 0;

        // Копируем Sequence Number (байты 4–7) из запроса в ответ
        memcpy(&reply[4], &buffer[4], 4);

        // Копируем Originate Timestamp (байты 8–15) из запроса в ответ
        memcpy(&reply[8], &buffer[8], 8);

        // Получаем текущее время
        NtpTimestamp now = get_ntp_time();
        uint32_t sec = htonl(now.seconds);
        uint32_t frac = htonl(now.fraction);

        // Receive Timestamp (байты 16–23)
        memcpy(&reply[16], &sec, 4);
        memcpy(&reply[20], &frac, 4);

        // Send Timestamp (байты 24–31)
        memcpy(&reply[24], &sec, 4);
        memcpy(&reply[28], &frac, 4);

        // Остальной payload (если был)
        if (recv_len > 32)
            memcpy(&reply[32], &buffer[32], recv_len - 32);

        sendto(sockfd, reply, recv_len, 0,
               (sockaddr*)&client_addr, client_len);
    }

    return 0;
}

