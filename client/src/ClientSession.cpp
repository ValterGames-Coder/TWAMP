#include "ClientSession.h"
#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>
#include <stdexcept>
#include <chrono>

ClientSession::ClientSession(uint32_t sid, int controlSocket, int testSocket)
    : sid_(sid), controlSocket_(controlSocket), testSocket_(testSocket), sequenceNumber_(0) {}

bool ClientSession::start() {
    std::vector<char> startSessions(12, 0);
    startSessions[0] = 7;  // Start-Sessions command
    
    if (send(controlSocket_, startSessions.data(), startSessions.size(), 0) != static_cast<ssize_t>(startSessions.size())) {
        std::cerr << "Failed to send Start-Sessions" << std::endl;
        return false;
    }
    
    std::vector<char> startAck(12);
    if (recv(controlSocket_, startAck.data(), startAck.size(), MSG_WAITALL) != static_cast<ssize_t>(startAck.size())) {
        std::cerr << "Failed to receive Start-Ack" << std::endl;
        return false;
    }
    
    return true;
}

bool ClientSession::stop() {
    std::vector<char> stopSessions(12, 0);
    stopSessions[0] = 4;  // Stop-Sessions command
    
    if (send(controlSocket_, stopSessions.data(), stopSessions.size(), 0) != static_cast<ssize_t>(stopSessions.size())) {
        std::cerr << "Failed to send Stop-Sessions" << std::endl;
        return false;
    }
    
    std::vector<char> stopAck(12);
    if (recv(controlSocket_, stopAck.data(), stopAck.size(), MSG_WAITALL) != static_cast<ssize_t>(stopAck.size())) {
        std::cerr << "Failed to receive Stop-Ack" << std::endl;
        return false;
    }
    
    return true;
}

void ClientSession::fillTimestamp(std::vector<char>& packet, size_t offset) {
    auto now = std::chrono::system_clock::now();
    auto since_epoch = now.time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(since_epoch);
    auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(since_epoch - seconds);
    
    uint32_t secs = htonl(static_cast<uint32_t>(seconds.count() + 2208988800UL));
    uint32_t frac = htonl(static_cast<uint32_t>((microseconds.count() << 32) / 1000000));
    
    memcpy(&packet[offset], &secs, 4);
    memcpy(&packet[offset + 4], &frac, 4);
}

bool ClientSession::sendTestPacket(uint32_t seqNumber, struct sockaddr_in& serverAddr) {
    std::vector<char> packet(64, 0);
    *reinterpret_cast<uint32_t*>(&packet[0]) = htonl(seqNumber);
    
    fillTimestamp(packet, 8);
    fillTimestamp(packet, 16);
    
    if (sendto(testSocket_, packet.data(), packet.size(), 0,
             reinterpret_cast<struct sockaddr*>(&serverAddr), sizeof(serverAddr)) != static_cast<ssize_t>(packet.size())) {
        std::cerr << "Failed to send test packet" << std::endl;
        return false;
    }
    
    return true;
}

bool ClientSession::receiveTestPacket(double& latencyMs) {
    std::vector<char> reflectedPacket(64);
    struct sockaddr_in fromAddr;
    socklen_t fromAddrLen = sizeof(fromAddr);
    
    // Установка таймаута
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(testSocket_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    ssize_t received = recvfrom(testSocket_, reflectedPacket.data(), reflectedPacket.size(), 0,
                              reinterpret_cast<struct sockaddr*>(&fromAddr), &fromAddrLen);
    
    if (received != static_cast<ssize_t>(reflectedPacket.size())) {
        std::cerr << "Failed to receive reflected packet: " 
                  << (received > 0 ? "incomplete" : strerror(errno)) 
                  << std::endl;
        return false;
    }
    
    // Используем монотонные часы для точности
    auto recv_time = std::chrono::steady_clock::now();
    
    uint64_t rx_timestamp;
    memcpy(&rx_timestamp, &reflectedPacket[32], sizeof(rx_timestamp));
    
    uint32_t rx_secs = ntohl(rx_timestamp >> 32);
    uint32_t rx_frac = ntohl(rx_timestamp & 0xFFFFFFFF);
    
    double rx_time = (rx_secs - 2208988800UL) + (rx_frac / 4294967296.0);
    
    // Время получения в секундах с высокой точностью
    auto since_epoch = recv_time.time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(since_epoch);
    auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(since_epoch - seconds);
    
    double recv_time_sec = seconds.count() + microseconds.count() / 1000000.0;
    latencyMs = (recv_time_sec - rx_time) * 1000;
    
    return true;
}