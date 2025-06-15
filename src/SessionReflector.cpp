#include "SessionReflector.hpp"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <syslog.h>
#include <cstring>
#include <cstdlib>

SessionReflector::SessionReflector(uint16_t port)
    : port_(port) 
{
    udp_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd_ < 0) {
        syslog(LOG_ERR, "Failed to create UDP socket: %m");
        exit(EXIT_FAILURE);
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(udp_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        syslog(LOG_ERR, "Failed to bind UDP socket to port %d: %m", port_);
        close(udp_fd_);
        exit(EXIT_FAILURE);
    }
}

SessionReflector::~SessionReflector() {
    close(udp_fd_);
}

void SessionReflector::run() {
    syslog(LOG_INFO, "SessionReflector listening on UDP port %d", port_);
    reflectLoop();
}

void SessionReflector::reflectLoop() {
    constexpr size_t BUFFER_SIZE = 2048;
    char buffer[BUFFER_SIZE];
    sockaddr_in client_addr{};
    socklen_t addr_len = sizeof(client_addr);

    while (true) {
        ssize_t len = recvfrom(udp_fd_, buffer, BUFFER_SIZE, 0,
                               reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
        if (len < 0) {
            syslog(LOG_ERR, "recvfrom error: %m");
            continue;
        }
        if (len == 0) continue; // возможно закрытие

        ssize_t sent = sendto(udp_fd_, buffer, len, 0,
                              reinterpret_cast<sockaddr*>(&client_addr), addr_len);
        if (sent != len) {
            syslog(LOG_ERR, "sendto error: %m");
        }
    }
}