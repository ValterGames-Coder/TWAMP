#include <iostream>
#include <csignal>
#include <cstring>
#include <cstdint>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <chrono>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>

constexpr int UDP_PORT = 20000;
constexpr const char* PID_FILE = "/var/run/twamp_light_reflector.pid";
volatile bool running = true;

// NTP epoch offset (1970 -> 1900)
constexpr uint64_t NTP_UNIX_EPOCH_OFFSET = 2208988800ULL;

struct NtpTimestamp {
    uint32_t seconds;
    uint32_t fraction;
};

NtpTimestamp get_ntp_time() {
    using namespace std::chrono;
    auto now = system_clock::now().time_since_epoch();
    auto secs = duration_cast<seconds>(now).count();
    auto nanos = duration_cast<nanoseconds>(now).count() % 1'000'000'000;

    uint32_t ntp_secs = static_cast<uint32_t>(secs + NTP_UNIX_EPOCH_OFFSET);
    uint32_t ntp_frac = static_cast<uint32_t>((nanos * (1LL << 32)) / 1'000'000'000);

    return {ntp_secs, ntp_frac};
}

void signal_handler(int) {
    running = false;
}

void write_pid() {
    FILE* f = fopen(PID_FILE, "w");
    if (f) {
        fprintf(f, "%d\n", getpid());
        fclose(f);
    } else {
        std::cerr << "Warning: cannot write PID file.\n";
    }
}

void daemonize() {
    if (fork() != 0) exit(0); // parent exits
    setsid();
    if (fork() != 0) exit(0); // second parent exits
    umask(0);
    chdir("/");
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    daemonize();
    write_pid();

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) return 1;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(UDP_PORT);

    if (bind(sockfd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        return 1;
    }

    char buffer[1500];
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);

    while (running) {
        ssize_t recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                                    (sockaddr*)&client_addr, &client_len);
        if (recv_len < 0) continue;

        // Assume RFC5357 format (e.g., sequence and timestamps are 32-bit aligned)
        if (recv_len >= 32) {
            // Copy incoming packet
            char reply[1500];
            memcpy(reply, buffer, recv_len);

            NtpTimestamp now = get_ntp_time();
            uint32_t secs = htonl(now.seconds);
            uint32_t frac = htonl(now.fraction);

            // Insert timestamps
            memcpy(&reply[16], &secs, 4); // Receive Timestamp
            memcpy(&reply[20], &frac, 4);
            memcpy(&reply[24], &secs, 4); // Send Timestamp
            memcpy(&reply[28], &frac, 4);

            sendto(sockfd, reply, recv_len, 0,
                   (sockaddr*)&client_addr, client_len);
        }
    }

    close(sockfd);
    unlink(PID_FILE);
    return 0;
}

