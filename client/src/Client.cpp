#include "Client.h"
#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <stdexcept>
#include <chrono>
#include <thread>
#include <random>

Client::Client(const std::string& serverAddress, int controlPort, int testPort)
    : serverAddress_(serverAddress), controlPort_(controlPort), testPort_(testPort),
      controlSocket_(-1), testSocket_(-1), sid_(0) {}

bool Client::runTest(int packetCount, int intervalMs) {
    try {
        if (!connectToServer()) {
            return false;
        }
        
        if (!performControlConnection()) {
            return false;
        }
        
        if (!setupTestSession()) {
            return false;
        }
        
        if (!startTestSession()) {
            return false;
        }
        
        if (!sendTestPackets(packetCount, intervalMs)) {
            return false;
        }
        
        if (!stopTestSession()) {
            return false;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return false;
    }
}

bool Client::connectToServer() {
    // Resolve server address
    memset(&serverAddr_, 0, sizeof(serverAddr_));
    serverAddr_.sin_family = AF_INET;
    serverAddr_.sin_port = htons(controlPort_);
    
    if (inet_pton(AF_INET, serverAddress_.c_str(), &serverAddr_.sin_addr) <= 0) {
        std::cerr << "Invalid server address" << std::endl;
        return false;
    }
    
    // Create control socket
    controlSocket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (controlSocket_ < 0) {
        std::cerr << "Failed to create control socket" << std::endl;
        return false;
    }
    
    // Connect to server
    if (connect(controlSocket_, (struct sockaddr*)&serverAddr_, sizeof(serverAddr_)) < 0) {
        std::cerr << "Failed to connect to server" << std::endl;
        return false;
    }
    
    // Create test socket
    testSocket_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (testSocket_ < 0) {
        std::cerr << "Failed to create test socket" << std::endl;
        return false;
    }
    
    return true;
}

bool Client::performControlConnection() {
    try {
        // 1. Set socket timeout
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(controlSocket_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        // 2. Send client greeting
        std::vector<char> greeting(12, 0);
        greeting[3] = 1;  // Unauthenticated mode
        
        if (send(controlSocket_, greeting.data(), greeting.size(), MSG_NOSIGNAL) != 
            static_cast<ssize_t>(greeting.size())) {
            throw std::runtime_error("Failed to send client greeting");
        }

        // 3. Receive server greeting
        std::vector<char> serverGreeting(12);
        ssize_t received = recv(controlSocket_, serverGreeting.data(), serverGreeting.size(), 0);
        
        if (received != static_cast<ssize_t>(serverGreeting.size())) {
            throw std::runtime_error(strerror(errno));
        }

        // 4. Verify server mode
        if (serverGreeting[3] != 1) {
            throw std::runtime_error("Unsupported server mode");
        }

        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Control connection error: " << e.what() << std::endl;
        return false;
    }
}

bool Client::setupTestSession() {
    // Установка таймаутов
    struct timeval tv;
    tv.tv_sec = 10; // Увеличиваем таймаут до 10 секунд
    tv.tv_usec = 0;
    
    setsockopt(controlSocket_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(controlSocket_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    // Send Request-Session (28 bytes)
    std::vector<char> requestSession(28, 0);
    requestSession[0] = 1;  // Request-Session command
    
    // Generate random SID
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis;
    sid_ = dis(gen);
    
    *reinterpret_cast<uint32_t*>(&requestSession[12]) = htonl(sid_);  // SID
    
    // Set test port
    struct sockaddr_in localAddr;
    socklen_t len = sizeof(localAddr);
    if (getsockname(testSocket_, (struct sockaddr*)&localAddr, &len) < 0) {
        std::cerr << "Failed to get socket name: " << strerror(errno) << std::endl;
        return false;
    }
    *reinterpret_cast<uint16_t*>(&requestSession[20]) = htons(ntohs(localAddr.sin_port));
    
    // Set client IP (use local address)
    *reinterpret_cast<uint32_t*>(&requestSession[24]) = localAddr.sin_addr.s_addr;
    
    // Send with timeout
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(controlSocket_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    ssize_t sent = send(controlSocket_, requestSession.data(), requestSession.size(), 0);
    if (sent != static_cast<ssize_t>(requestSession.size())) {
        std::cerr << "Failed to send Request-Session: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Receive with timeout
    setsockopt(controlSocket_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    std::vector<char> acceptSession(28);
    ssize_t received = recv(controlSocket_, acceptSession.data(), acceptSession.size(), 0);
    
    if (received != static_cast<ssize_t>(acceptSession.size())) {
        std::cerr << "Failed to receive Accept-Session: " 
                  << (received > 0 ? "incomplete" : strerror(errno)) 
                  << std::endl;
        return false;
    }
    
    // Check if session was accepted (byte 16 should be 0)
    if (acceptSession[16] != 0) {
        std::cerr << "Session was not accepted by server (code: " 
                  << static_cast<int>(acceptSession[16]) << ")" << std::endl;
        return false;
    }
    
    return true;
}

bool Client::startTestSession() {
    // Send Start-Sessions (12 bytes)
    std::vector<char> startSessions(12, 0);
    startSessions[0] = 7;  // Start-Sessions command
    
    if (send(controlSocket_, startSessions.data(), startSessions.size(), 0) != startSessions.size()) {
        std::cerr << "Failed to send Start-Sessions" << std::endl;
        return false;
    }
    
    // Receive Start-Ack (12 bytes)
    std::vector<char> startAck(12);
    if (recv(controlSocket_, startAck.data(), startAck.size(), MSG_WAITALL) != startAck.size()) {
        std::cerr << "Failed to receive Start-Ack" << std::endl;
        return false;
    }
    
    return true;
}

bool Client::stopTestSession() {
    // Send Stop-Sessions (12 bytes)
    std::vector<char> stopSessions(12, 0);
    stopSessions[0] = 4;  // Stop-Sessions command
    
    if (send(controlSocket_, stopSessions.data(), stopSessions.size(), 0) != stopSessions.size()) {
        std::cerr << "Failed to send Stop-Sessions" << std::endl;
        return false;
    }
    
    // Receive Stop-Ack (12 bytes)
    std::vector<char> stopAck(12);
    if (recv(controlSocket_, stopAck.data(), stopAck.size(), MSG_WAITALL) != stopAck.size()) {
        std::cerr << "Failed to receive Stop-Ack" << std::endl;
        return false;
    }
    
    return true;
}

bool Client::sendTestPackets(int packetCount, int intervalMs) {
    // Prepare test packet (64 bytes as per TWAMP standard)
    std::vector<char> packet(64, 0);
    
    // Sequence number (bytes 0-3)
    uint32_t sequenceNumber = 0;
    
    // Timestamp (bytes 8-15 and 16-23 for both timestamp fields)
    auto fillTimestamp = [](std::vector<char>& pkt, size_t offset) {
        auto now = std::chrono::system_clock::now();
        auto since_epoch = now.time_since_epoch();
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(since_epoch);
        auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(since_epoch - seconds);
        
        uint32_t secs = htonl(seconds.count() + 2208988800UL);  // NTP epoch
        uint32_t frac = htonl(static_cast<uint32_t>((microseconds.count() << 32) / 1000000));
        
        memcpy(&pkt[offset], &secs, 4);
        memcpy(&pkt[offset + 4], &frac, 4);
    };
    
    // Set test server address (using control port + 1)
    struct sockaddr_in testServerAddr = serverAddr_;
    testServerAddr.sin_port = htons(testPort_);
    
    for (int i = 0; i < packetCount; ++i) {
        // Update sequence number
        *reinterpret_cast<uint32_t*>(&packet[0]) = htonl(sequenceNumber++);
        
        // Update timestamps
        fillTimestamp(packet, 8);  // Timestamp
        fillTimestamp(packet, 16); // Error estimate (we just use the same timestamp)
        
        // Send packet
        if (sendto(testSocket_, packet.data(), packet.size(), 0,
                 (struct sockaddr*)&testServerAddr, sizeof(testServerAddr)) != packet.size()) {
            std::cerr << "Failed to send test packet " << i << std::endl;
            return false;
        }
        
        // Receive reflected packet
        std::vector<char> reflectedPacket(64);
        struct sockaddr_in fromAddr;
        socklen_t fromAddrLen = sizeof(fromAddr);
        
        ssize_t received = recvfrom(testSocket_, reflectedPacket.data(), reflectedPacket.size(), 0,
                                  (struct sockaddr*)&fromAddr, &fromAddrLen);
        
        if (received != reflectedPacket.size()) {
            std::cerr << "Failed to receive reflected packet " << i << std::endl;
            return false;
        }
        
        // Calculate latency (simplified)
        auto now = std::chrono::system_clock::now();
        auto now_s = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());
        auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch() - now_s);
        
        uint32_t rx_secs = ntohl(*reinterpret_cast<uint32_t*>(&reflectedPacket[32]));
        uint32_t rx_frac = ntohl(*reinterpret_cast<uint32_t*>(&reflectedPacket[36]));
        
        double rx_time = (rx_secs - 2208988800UL) + (rx_frac / 4294967296.0);
        double tx_time = (now_s.count() - 2208988800UL) + (now_us.count() / 1000000.0);
        double latency = (tx_time - rx_time) * 1000;  // in milliseconds
        
        std::cout << "Packet " << i << ": Latency = " << latency << " ms" << std::endl;
        
        if (i < packetCount - 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
        }
    }
    
    return true;
}