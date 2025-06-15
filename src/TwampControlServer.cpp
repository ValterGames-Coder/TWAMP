#include "TwampControlServer.hpp"
#include "SessionReflector.hpp"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <syslog.h>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

// Структуры для TWAMP-Control (упрощённо, без защиты)
#pragma pack(push,1)
struct Greeting 
{
    uint8_t version;
    uint8_t reserved;
    uint16_t modes;
};

struct RequestTWSession 
{
    uint8_t type;       // 0x00
    uint8_t reserved;
    uint16_t count;
    uint16_t sender_port;  // исправлено на 16 бит
    uint16_t reserved2;
};

struct ControlReply 
{
    uint8_t type;       // 0x00 = OK
    uint8_t reserved;
    uint16_t reserved2;
    uint16_t reflect_port; // исправлено на 16 бит
};
#pragma pack(pop)

TwampControlServer::TwampControlServer(uint16_t port) 
{
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        syslog(LOG_ERR, "Failed to create socket: %m");
        exit(EXIT_FAILURE);
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        syslog(LOG_ERR, "Failed to bind socket to port %d: %m", port);
        close(listen_fd_);
        exit(EXIT_FAILURE);
    }

    if (listen(listen_fd_, 5) < 0) {
        syslog(LOG_ERR, "Failed to listen on port %d: %m", port);
        close(listen_fd_);
        exit(EXIT_FAILURE);
    }

    syslog(LOG_INFO, "Socket successfully created, bound, and listening on port %d", port);
}

TwampControlServer::~TwampControlServer() 
{
    close(listen_fd_);
}

void TwampControlServer::run()
{
    syslog(LOG_INFO, "TWAMP-Control server running on port 862");
    acceptLoop();
}

void TwampControlServer::acceptLoop() 
{
    while (true) 
    {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(listen_fd_, (sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) {
            syslog(LOG_ERR, "Accept failed: %m");
            continue;
        }

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
        syslog(LOG_INFO, "New client connected from %s", ip);

        std::thread(&TwampControlServer::handleClient, this, client_fd).detach();
    }
}

void TwampControlServer::handleClient(int client_fd) 
{
    // Установка таймаута 5 секунд на recv
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    if (setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        syslog(LOG_ERR, "Failed to set recv timeout: %m");
        close(client_fd);
        return;
    }

    try 
    {
        negotiateControl(client_fd);
        processControlCommands(client_fd);
    } 
    catch (const std::exception& ex) 
    {
        syslog(LOG_ERR, "Client handling error: %s", ex.what());
    }
    close(client_fd);
}

void TwampControlServer::negotiateControl(int client_fd)
{
    Greeting clientGreet{};
    ssize_t len = recv(client_fd, &clientGreet, sizeof(clientGreet), MSG_WAITALL);
    if (len == 0) {
        syslog(LOG_ERR, "Client closed connection unexpectedly while reading Greeting");
        throw std::runtime_error("Client closed connection on Greeting");
    }
    if (len < 0) {
        syslog(LOG_ERR, "Error reading Greeting: %m");
        throw std::runtime_error("recv error on Greeting");
    }
    if (len != sizeof(clientGreet)) {
        syslog(LOG_ERR, "Incomplete Greeting received (%zd bytes)", len);
        throw std::runtime_error("Incomplete Greeting");
    }

    Greeting serverGreet{ clientGreet.version, 0, 0 };
    ssize_t sent = send(client_fd, &serverGreet, sizeof(serverGreet), 0);
    if (sent != sizeof(serverGreet)) {
        syslog(LOG_ERR, "Failed to send Greeting response");
        throw std::runtime_error("send error on Greeting response");
    }
    syslog(LOG_DEBUG, "Greeting exchanged: version %d", serverGreet.version);
}

void TwampControlServer::processControlCommands(int client_fd) 
{
    RequestTWSession req{};
    ssize_t len = recv(client_fd, &req, sizeof(req), MSG_WAITALL);
    if (len == 0) {
        syslog(LOG_ERR, "Client closed connection unexpectedly while reading Request-TW-Session");
        throw std::runtime_error("Client closed connection on Request-TW-Session");
    }
    if (len < 0) {
        syslog(LOG_ERR, "Error reading Request-TW-Session: %m");
        throw std::runtime_error("recv error on Request-TW-Session");
    }
    if (len != sizeof(req)) {
        syslog(LOG_ERR, "Incomplete Request-TW-Session received (%zd bytes)", len);
        throw std::runtime_error("Incomplete Request-TW-Session");
    }

    uint16_t reflect_port = ntohs(req.sender_port);
    syslog(LOG_INFO, "Session requested on port %d", reflect_port);

    ControlReply reply{ 0x00, 0, 0, htons(reflect_port) };
    ssize_t sent = send(client_fd, &reply, sizeof(reply), 0);
    if (sent != sizeof(reply)) {
        syslog(LOG_ERR, "Failed to send ControlReply");
        throw std::runtime_error("send error on ControlReply");
    }
    syslog(LOG_DEBUG, "Sent ControlReply for port %d", reflect_port);

    uint8_t cmd;
    len = recv(client_fd, &cmd, 1, MSG_WAITALL);
    if (len <= 0) {
        syslog(LOG_ERR, "Failed to read Start command or client closed connection");
        throw std::runtime_error("Failed to read Start command");
    }
    if (cmd != 0x01) {
        syslog(LOG_ERR, "Expected Start command (0x01), got 0x%02x", cmd);
        throw std::runtime_error("Invalid Start command");
    }

    syslog(LOG_INFO, "Start command received, launching reflector");

    SessionReflector reflector(reflect_port);
    reflector.run();
}