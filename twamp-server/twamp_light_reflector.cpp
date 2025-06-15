#include <iostream>
#include <cstring>
#include <chrono>
#include <csignal>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define TWAMP_PORT 20000
#define NTP_UNIX_OFFSET 2208988800UL

volatile bool running = true;

struct NtpTimestamp {
    uint32_t seconds;
    uint32_t fraction;
} __attribute__((packed));

struct TwampTestPacket {
    uint32_t seq_num;
    NtpTimestamp timestamp; // T1: Client send time
    char payload[56];       // Payload (optional)
} __attribute__((packed));

struct TwampReflectPacket {
    uint32_t seq_num;
    NtpTimestamp timestamp; // T2: Reflector receive time
    char payload[56];
} __attribute__((packed));

void handle_signal(int signal) {
    running = false;
}

NtpTimestamp get_ntp_time() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto since_epoch = now.time_since_epoch();

    auto sec = duration_cast<seconds>(since_epoch).count();
    auto nano = duration_cast<nanoseconds>(since_epoch).count() % 1'000'000'000;

    uint32_t seconds = (uint32_t)(sec + NTP_UNIX_OFFSET);
    uint32_t fraction = (uint32_t)(((uint64_t)nano << 32) / 1'000'000'000);

    return NtpTimestamp{ seconds, fraction };
}

int main() {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::cerr << "Socket creation failed\n";
        return 1;
    }

    sockaddr_in servaddr{};
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(TWAMP_PORT);

    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        std::cerr << "Bind failed\n";
        return 1;
    }

    std::cout << "TWAMP Light Reflector started on port " << TWAMP_PORT << std::endl;

    while (running) {
        sockaddr_in cliaddr{};
        socklen_t len = sizeof(cliaddr);
        TwampTestPacket request{};
        ssize_t n = recvfrom(sockfd, &request, sizeof(request), 0,
                             (struct sockaddr *)&cliaddr, &len);
        if (n < 0) continue;

        TwampReflectPacket response{};
        response.seq_num = request.seq_num;
        response.timestamp = get_ntp_time();
        memcpy(response.payload, request.payload, sizeof(response.payload));

        sendto(sockfd, &response, sizeof(response), 0,
               (const struct sockaddr *)&cliaddr, len);
    }

    close(sockfd);
    std::cout << "TWAMP Reflector stopped.\n";
    return 0;
}

