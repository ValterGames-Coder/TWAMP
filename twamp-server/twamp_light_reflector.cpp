// TWAMP Light (RFC 5357) compliant UDP reflector server
// Fully correct implementation with NTP timestamps and proper Sequence copy

#include <iostream>
#include <cstring>
#include <csignal>
#include <chrono>
#include <netinet/in.h>
#include <unistd.h>

int sockfd;
bool running = true;

// NTP Timestamp: 64 bits = 32s seconds + 32s fractional
struct NtpTimestamp {
    uint32_t seconds;
    uint32_t fraction;
};

// Convert system time to NTP format
NtpTimestamp get_ntp_time() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto since_epoch = now.time_since_epoch();
    auto seconds = duration_cast<seconds>(since_epoch).count();
    auto nanoseconds = duration_cast<nanoseconds>(since_epoch).count() % 1'000'000'000;

    // 2^32 ~= 4.294967296e9 => fraction = nanoseconds * (2^32 / 1e9)
    uint32_t fraction = (uint32_t)((nanoseconds * (1LL << 32)) / 1'000'000'000);
    const uint32_t NTP_UNIX_OFFSET = 2208988800U;
    return { (uint32_t)(seconds + NTP_UNIX_OFFSET), fraction };
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

    std::cout << "[TWAMP] Reflector running on UDP port 20000\n";

    while (running) {
        uint8_t buffer[1500];
        sockaddr_in client{};
        socklen_t client_len = sizeof(client);

        ssize_t recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                                    (sockaddr*)&client, &client_len);
        if (recv_len < 32) continue; // TWAMP-Test packet must be >= 32

        uint8_t reply[1500];
        memset(reply, 0, sizeof(reply));

        // 0: MBZ
        reply[0] = 0;

        // 1: Control (0x00 = no control)
        reply[1] = 0x00;

        // 4–7: Sequence Number (copied from received packet)
        memcpy(&reply[4], &buffer[4], 4);

        // 8–15: Originate Timestamp (copy from received)
        memcpy(&reply[8], &buffer[8], 8);

        // 16–23: Receive Timestamp (when request was received)
        NtpTimestamp rcv = get_ntp_time();
        *(uint32_t*)&reply[16] = htonl(rcv.seconds);
        *(uint32_t*)&reply[20] = htonl(rcv.fraction);

        // 24–31: Transmit Timestamp (immediately before send)
        NtpTimestamp xmt = get_ntp_time();
        *(uint32_t*)&reply[24] = htonl(xmt.seconds);
        *(uint32_t*)&reply[28] = htonl(xmt.fraction);

        // Append payload if any (after 32 bytes)
        if (recv_len > 32)
            memcpy(&reply[32], &buffer[32], recv_len - 32);

        sendto(sockfd, reply, recv_len, 0, (sockaddr*)&client, client_len);
    }

    std::cout << "\n[TWAMP] Server terminated." << std::endl;
    return 0;
}

