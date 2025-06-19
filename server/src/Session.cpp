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
            // Read the first byte to determine message type and size
            char firstByte;
            ssize_t received = recv(controlSocket_, &firstByte, 1, 0);
            
            if (received == 0) {
                // Client closed connection gracefully
                std::cout << "Client closed connection" << std::endl;
                break;
            } else if (received != 1) {
                throw std::runtime_error("Failed to receive first byte");
            }
            
            lastActivity_ = std::chrono::steady_clock::now();
            
            // Based on first byte, determine message size and read the rest
            switch (firstByte) {
                case 1:  // Request-Session (28 bytes total)
                    {
                        std::vector<char> message(27);
                        if (recv(controlSocket_, message.data(), 27, MSG_WAITALL) != 27) {
                            throw std::runtime_error("Failed to receive Request-Session data");
                        }
                        handleRequestSession(message);
                    }
                    break;
                case 7:  // Start-Sessions (12 bytes total)
                    {
                        std::vector<char> message(11);
                        if (recv(controlSocket_, message.data(), 11, MSG_WAITALL) != 11) {
                            throw std::runtime_error("Failed to receive Start-Sessions data");
                        }
                        handleStartSessions();
                    }
                    break;
                case 4:  // Stop-Sessions (12 bytes total)
                    {
                        std::vector<char> message(11);
                        if (recv(controlSocket_, message.data(), 11, MSG_WAITALL) != 11) {
                            throw std::runtime_error("Failed to receive Stop-Sessions data");
                        }
                        handleStopSessions();
                        // After stop sessions, expect client to close connection
                        return;
                    }
                    break;
                default:
                    std::cerr << "Unknown command: " << static_cast<int>(firstByte) << std::endl;
                    throw std::runtime_error("Unknown command");
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Session error: " << e.what() << std::endl;
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
    // FIXED: Match based on client's source IP, not the stored port
    // The client sends from a dynamic port to our test port, so we match by IP
    return testActive_ && addr.sin_addr.s_addr == testClientAddr_.sin_addr.s_addr;
}

void Session::processTestPacket(const char* data, size_t size, const struct sockaddr_in& fromAddr) {
    if (!testActive_) {
        std::cout << "Received test packet but session not active" << std::endl;
        return;
    }
    
    std::cout << "Processing test packet from " << inet_ntoa(fromAddr.sin_addr) 
              << ":" << ntohs(fromAddr.sin_port) << " (size: " << size << ")" << std::endl;
    
    auto reflectorPacket = generateReflectorPacket(data, size, fromAddr);
    
    // Send back to the client's source address and port
    ssize_t sent = sendto(testSocket_, reflectorPacket.data(), reflectorPacket.size(), 0,
                         (struct sockaddr*)&fromAddr, sizeof(fromAddr));
    
    if (sent < 0) {
        std::cerr << "Failed to send reflector packet: " << strerror(errno) << std::endl;
    } else {
        std::cout << "Sent reflector packet back to " << inet_ntoa(fromAddr.sin_addr) 
                  << ":" << ntohs(fromAddr.sin_port) << " (" << sent << " bytes)" << std::endl;
    }
}

std::vector<char> Session::generateReflectorPacket(const char* data, size_t size, const sockaddr_in& fromAddr) {
    std::vector<char> packet(data, data + size);
    
    // For TWAMP reflector packets, we need to add receive and transmit timestamps
    if (size >= 64) {  // Standard TWAMP test packet size
        // Get current time in NTP format
        auto now = std::chrono::system_clock::now();
        auto since_epoch = now.time_since_epoch();
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(since_epoch);
        auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(since_epoch - seconds);
        
        uint32_t secs = htonl(static_cast<uint32_t>(seconds.count() + 2208988800UL));  // NTP epoch
        uint32_t frac = htonl(static_cast<uint32_t>((microseconds.count() << 32) / 1000000));
        
        // Receive timestamp (bytes 16-23) - when we received the packet
        memcpy(&packet[16], &secs, 4);
        memcpy(&packet[20], &frac, 4);
        
        // Transmit timestamp (bytes 24-31) - when we're sending it back
        // For simplicity, use the same timestamp (processing time is minimal)
        memcpy(&packet[24], &secs, 4);
        memcpy(&packet[28], &frac, 4);
        
        // Sequence number is already in the packet (bytes 0-3)
        // Sender timestamp is already in the packet (bytes 8-15)
    }
    
    return packet;
}

void Session::handleRequestSession(const std::vector<char>& message) {
    try {
        // Parse SID from bytes 11-14 (since we already read the command byte)
        sid_ = ntohl(*reinterpret_cast<const uint32_t*>(&message[11]));
        
        // Parse test client port from bytes 19-20 (adjusted for removed command byte)
        uint16_t clientPort = *reinterpret_cast<const uint16_t*>(&message[19]); // Keep in network order
        
        // Parse client IP from bytes 23-26 (adjusted for removed command byte)  
        uint32_t clientIP = *reinterpret_cast<const uint32_t*>(&message[23]); // Keep in network order
        
        // Set up test client address - store the client's address for matching
        testClientAddr_.sin_family = AF_INET;
        testClientAddr_.sin_port = clientPort;  // Client's port
        testClientAddr_.sin_addr.s_addr = clientIP;  // Client's IP
        
        std::cout << "Request-Session: SID=" << sid_ 
                  << ", Client=" << inet_ntoa(testClientAddr_.sin_addr) 
                  << ":" << ntohs(testClientAddr_.sin_port) << std::endl;
        
        // Send Accept-Session (28 bytes)
        std::vector<char> acceptMessage(28, 0);
        acceptMessage[0] = 3;  // Accept-Session command
        *reinterpret_cast<uint32_t*>(&acceptMessage[12]) = htonl(sid_);  // Echo back SID
        acceptMessage[16] = 0;  // Accept (0 means accepted)
        
        sendControlMessage(acceptMessage);
        
    } catch (const std::exception& e) {
        std::cerr << "Request-Session error: " << e.what() << std::endl;
        throw;
    }
}

void Session::handleStartSessions() {
    std::cout << "Start-Sessions received for SID=" << sid_ << std::endl;
    
    // Send Start-Ack (12 bytes)
    std::vector<char> startAck(12, 0);
    startAck[0] = 8;  // Start-Ack command
    
    sendControlMessage(startAck);
    
    testActive_ = true;
    std::cout << "Test session activated for client " << inet_ntoa(testClientAddr_.sin_addr) << std::endl;
}

void Session::handleStopSessions() {
    std::cout << "Stop-Sessions received for SID=" << sid_ << std::endl;
    
    // Send Stop-Ack (12 bytes)
    std::vector<char> stopAck(12, 0);
    stopAck[0] = 9;  // Stop-Ack command
    
    sendControlMessage(stopAck);
    
    testActive_ = false;
    std::cout << "Test session stopped for SID=" << sid_ << std::endl;
}

void Session::sendControlMessage(const std::vector<char>& message) {
    if (send(controlSocket_, message.data(), message.size(), 0) != static_cast<ssize_t>(message.size())) {
        throw std::runtime_error("Failed to send control message");
    }
}

// Remove unused methods
void Session::handleSetUpResponse() {
    // Not used in this implementation
}

void Session::handleServerStart() {
    // Not used in this implementation
}

void Session::handleAcceptSession() {
    // Not used in this implementation
}

void Session::handleFetchSession() {
    // Not used in this implementation
}

std::vector<char> Session::receiveControlMessage(size_t expectedSize) {
    std::vector<char> message(expectedSize);
    
    if (recv(controlSocket_, message.data(), expectedSize, MSG_WAITALL) != static_cast<ssize_t>(expectedSize)) {
        throw std::runtime_error("Failed to receive control message");
    }
    
    return message;
}