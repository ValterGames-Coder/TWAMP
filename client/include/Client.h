#ifndef TWAMP_CLIENT_H
#define TWAMP_CLIENT_H

#include <string>
#include <netinet/in.h>
#include <vector>

class Client {
public:
    Client(const std::string& serverAddress, int controlPort = 862, int testPort = 863);
    bool runTest(int packetCount = 10, int intervalMs = 1000);
    
private:
    bool connectToServer();
    bool performControlConnection();
    bool setupTestSession();
    bool startTestSession();
    bool stopTestSession();
    bool sendTestPackets(int packetCount, int intervalMs);
    
    std::string serverAddress_;
    int controlPort_;
    int testPort_;
    int controlSocket_;
    int testSocket_;
    struct sockaddr_in serverAddr_;
    uint32_t sid_;
};

#endif // TWAMP_CLIENT_H