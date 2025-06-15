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

#pragma pack(push, 1)
struct NtpTimestamp {
    uint32_t seconds;
    uint32_t fraction;
};

struct TwampTestPacket {
    uint32_t seq_num;
    NtpTimestamp timestamp;
    char payload[56];
};

struct TwampReflectPacket {
    uint32_t seq_num;
    NtpTimestamp timestamp;
    char payload[56];
};
#pragma pack(pop)

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

    return NtpTimestamp{htonl(seconds), htonl(fraction)};
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

        if (n != sizeof(request)) {
            std::cerr << "Received incorrect packet size: " << n << std::endl;
            continue;
        }

        TwampReflectPacket response{};
        response.seq_num = request.seq_num; // copy sequence
        response.timestamp = get_ntp_time(); // server receive timestamp
        memcpy(response.payload, request.payload, sizeof(request.payload));

        ssize_t sent = sendto(sockfd, &response, sizeof(response), 0,
                              (const struct sockaddr *)&cliaddr, len);
        if (sent != sizeof(response)) {
            std::cerr << "Failed to send response\n";
        }
    }

    close(sockfd);
    std::cout << "TWAMP Reflector stopped.\n";
    return 0;
}

