#ifndef TWAMP_CLIENT_SESSION_H
#define TWAMP_CLIENT_SESSION_H

#include <vector>
#include <cstdint>
#include <chrono>
#include <netinet/in.h>

class ClientSession {
public:
    ClientSession(uint32_t sid, int controlSocket, int testSocket);
    
    bool start();
    bool stop();
    bool sendTestPacket(uint32_t seqNumber, struct sockaddr_in& serverAddr);
    bool receiveTestPacket(double& latencyMs);
    
    uint32_t getSid() const { return sid_; }

private:
    uint32_t sid_;
    int controlSocket_;
    int testSocket_;
    uint32_t sequenceNumber_;
    
    void fillTimestamp(std::vector<char>& packet, size_t offset);
};

#endif // TWAMP_CLIENT_SESSION_H