#include <iostream>
#include <chrono>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>

constexpr int PORT = 20000;
constexpr int MAX_PACKET_SIZE = 1500;
constexpr int REQUIRED_PACKET_SIZE = 48;

constexpr uint64_t NTP_UNIX_OFFSET = 2208988800ULL;

struct NtpTimestamp {
    uint32_t seconds;
    uint32_t fraction;
};

NtpTimestamp get_ntp_time() {
    using namespace std::chrono;
    auto now = system_clock::now().time_since_epoch();
    auto secs = duration_cast<seconds>(now).count();
    auto nanos = duration_cast<nanoseconds>(now).count() % 1'000'000'000;

    uint32_t fraction = (uint64_t(nanos) << 32) / 1'000'000'000;
    return { static_cast<uint32_t>(secs + NTP_UNIX_OFFSET), fraction };
}

int main() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in addr{}, client_addr{};
    socklen_t addr_len = sizeof(client_addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        close(sockfd);
        return 1;
    }

    std::cout << "TWAMP Light Reflector listening on port " << PORT << "...\n";

    uint8_t buffer[MAX_PACKET_SIZE];

    while (true) {
        ssize_t len = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                               reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
        if (len < REQUIRED_PACKET_SIZE) {
            std::cerr << "Received too short packet (" << len << " bytes)\n";
            continue;
        }

        // Parse client fields
        uint32_t seq_num;
        std::memcpy(&seq_num, buffer, 4);
        seq_num = ntohl(seq_num);

        NtpTimestamp client_ts;
        std::memcpy(&client_ts, buffer + 4, sizeof(NtpTimestamp));
        client_ts.seconds = ntohl(client_ts.seconds);
        client_ts.fraction = ntohl(client_ts.fraction);

        std::cout << "Received seq=" << seq_num << "\n";

        // Fill response
        uint8_t response[MAX_PACKET_SIZE] = {};
        uint32_t resp_seq = htonl(seq_num);
        std::memcpy(response, &resp_seq, 4);  // Sequence Number

        NtpTimestamp recv_time = get_ntp_time();
        uint32_t ts_sec = htonl(recv_time.seconds);
        uint32_t ts_frac = htonl(recv_time.fraction);
        std::memcpy(response + 12, &ts_sec, 4);     // Receive Timestamp (sec)
        std::memcpy(response + 16, &ts_frac, 4);    // Receive Timestamp (frac)

        uint32_t snd_seq = htonl(seq_num);
        std::memcpy(response + 20, &snd_seq, 4);    // Sender Sequence Number

        std::memcpy(response + 24, &ts_sec, 4);     // Sender Timestamp (sec)
        std::memcpy(response + 28, &ts_frac, 4);    // Sender Timestamp (frac)

        uint32_t ttl = 255;
        std::memcpy(response + 36, &ttl, 1);        // Sender TTL (1 byte)

        // Echo padding if present
        if (len > REQUIRED_PACKET_SIZE) {
            std::memcpy(response + 40, buffer + 40, len - 40);
        }

        ssize_t sent = sendto(sockfd, response, len, 0,
                              reinterpret_cast<sockaddr*>(&client_addr), addr_len);
        if (sent < 0) {
            perror("sendto");
        }
    }

    close(sockfd);
    return 0;
}

