#include "Session.h"
#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>
#include <stdexcept>
#include <chrono>

Session::Session(int controlSocket, int testSocket) 
    : controlSocket_(controlSocket), testSocket_(testSocket), testActive_(false) {
    lastActivity_ = std::chrono::steady_clock::now();
    sid_ = 0;
    memset(&testClientAddr_, 0, sizeof(testClientAddr_));
}

Session::~Session() {
    if (controlSocket_ != -1) {
        close(controlSocket_);
    }
}

void Session::run() {
    try {
        while (true) {
            // First byte is command
            char command;
            if (recv(controlSocket_, &command, 1, 0) != 1) {
                throw std::runtime_error("Failed to receive command");
            }
            
            lastActivity_ = std::chrono::steady_clock::now();
            
            switch (command) {
                case 2:  // Set-Up-Response
                    handleSetUpResponse();
                    break;
                case 5:  // Server-Start
                    handleServerStart();
                    break;
                case 1:  // Request-Session
                    handleRequestSession();
                    break;
                case 3:  // Accept-Session
                    handleAcceptSession();
                    break;
                case 7:  // Start-Sessions
                    handleStartSessions();
                    break;
                case 4:  // Stop-Sessions
                    handleStopSessions();
                    break;
                case 6:  // Fetch-Session
                    handleFetchSession();
                    break;
                default:
                    throw std::runtime_error("Unknown command");
            }
        }
    } catch (const std::exception& e) {
        if (testActive_) {
            testActive_ = false;
        }
        throw;
    }
}

bool Session::isExpired() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::minutes>(now - lastActivity_).count() > 5;
}

bool Session::matchesTestAddress(const struct sockaddr_in& addr) const {
    return testActive_ && 
           addr.sin_addr.s_addr == testClientAddr_.sin_addr.s_addr &&
           addr.sin_port == testClientAddr_.sin_port;
}

void Session::processTestPacket(const char* data, size_t size, const struct sockaddr_in& fromAddr) {
    if (!testActive_) return;
    
    auto reflectorPacket = generateReflectorPacket(data, size, fromAddr);
    sendto(testSocket_, reflectorPacket.data(), reflectorPacket.size(), 0,
          (struct sockaddr*)&testClientAddr_, sizeof(testClientAddr_));
}

std::vector<char> Session::generateReflectorPacket(const char* data, size_t size, const sockaddr_in& fromAddr) {
    std::vector<char> packet(data, data + size);
    
    // Для UDP-пакетов TWAMP минимальный размер - 41 байт
    if (size >= 41) {
        // Обновляем временные метки
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        
        uint64_t ntp_ts = ((uint64_t)(ts.tv_sec + 2208988800UL) << 32) |
                          (uint64_t)((ts.tv_nsec * 4294967296ULL) / 1000000000ULL);
        
        // Receive timestamp (bytes 32-39)
        memcpy(&packet[32], &ntp_ts, sizeof(ntp_ts));
        
        // Transmit timestamp (bytes 40-47)
        memcpy(&packet[40], &ntp_ts, sizeof(ntp_ts));
    }
    
    return packet;
}

void Session::handleSetUpResponse() {
    // Receive Set-Up-Response message (112 bytes)
    auto message = receiveControlMessage(112);
    
    // Just acknowledge it (no specific processing needed in unauthenticated mode)
}

void Session::handleServerStart() {
    // Receive Server-Start message (112 bytes)
    auto message = receiveControlMessage(112);
    
    // Just acknowledge it (no specific processing needed in unauthenticated mode)
}

void Session::handleRequestSession() {
    // Receive Request-Session message (28 bytes)
    auto message = receiveControlMessage(28);
    
    // Parse SID (Session ID)
    sid_ = ntohl(*reinterpret_cast<uint32_t*>(&message[12]));
    
    // Parse test port and address
    testClientAddr_.sin_family = AF_INET;
    testClientAddr_.sin_port = htons(ntohs(*reinterpret_cast<uint16_t*>(&message[20])));
    testClientAddr_.sin_addr.s_addr = *reinterpret_cast<uint32_t*>(&message[24]);
    
    // Send Accept-Session (28 bytes)
    std::vector<char> acceptMessage(28, 0);
    *reinterpret_cast<uint32_t*>(&acceptMessage[12]) = htonl(sid_);  // SID
    acceptMessage[16] = 0;  // Accept (0 means accepted)
    
    sendControlMessage(acceptMessage);
}

void Session::handleAcceptSession() {
    // Receive Accept-Session message (28 bytes)
    auto message = receiveControlMessage(28);
    
    // Just acknowledge it (no specific processing needed in unauthenticated mode)
}

void Session::handleStartSessions() {
    // Receive Start-Sessions message (12 bytes)
    auto message = receiveControlMessage(12);
    
    // Send Start-Ack (12 bytes)
    std::vector<char> startAck(12, 0);
    startAck[0] = 8;  // Start-Ack command
    
    sendControlMessage(startAck);
    
    testActive_ = true;
}

void Session::handleStopSessions() {
    // Receive Stop-Sessions message (12 bytes)
    auto message = receiveControlMessage(12);
    
    // Send Stop-Ack (12 bytes)
    std::vector<char> stopAck(12, 0);
    stopAck[0] = 9;  // Stop-Ack command
    
    sendControlMessage(stopAck);
    
    testActive_ = false;
}

void Session::handleFetchSession() {
    // Receive Fetch-Session message (12 bytes)
    auto message = receiveControlMessage(12);
    
    // Send Fetch-Response (36 bytes)
    std::vector<char> fetchResponse(36, 0);
    fetchResponse[0] = 10;  // Fetch-Response command
    
    // For simplicity, we just return zeros for all statistics
    sendControlMessage(fetchResponse);
}

void Session::sendControlMessage(const std::vector<char>& message) {
    if (send(controlSocket_, message.data(), message.size(), 0) != static_cast<ssize_t>(message.size())) {
        throw std::runtime_error("Failed to send control message");
    }
}

std::vector<char> Session::receiveControlMessage(size_t expectedSize) {
    std::vector<char> message(expectedSize);
    
    if (recv(controlSocket_, message.data(), expectedSize, MSG_WAITALL) != static_cast<ssize_t>(expectedSize)) {
        throw std::runtime_error("Failed to receive control message");
    }
    
    return message;
}