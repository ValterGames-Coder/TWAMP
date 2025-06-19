#ifndef TWAMP_CLIENT_H
#define TWAMP_CLIENT_H

#include <string>
#include <netinet/in.h>
#include <vector>

class Client {
public:
    Client(const std::string& serverAddress, int controlPort, int testPort);
    ~Client();
    
    bool runTest(int packetCount, int intervalMs);
    
private:
    std::string serverAddress_;
    int controlPort_;
    int testPort_;
    
    int controlSocket_;
    int testSocket_;
    struct sockaddr_in serverAddr_;
    uint32_t sid_;
    
    bool connectToServer();
    bool performControlConnection();
    bool setupTestSession();
    bool startTestSession();
    bool stopTestSession();
    bool sendTestPackets(int packetCount, int intervalMs);
};

#endif // TWAMP_CLIENT_H