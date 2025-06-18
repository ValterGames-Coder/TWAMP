#ifndef TWAMP_SESSION_H
#define TWAMP_SESSION_H

#include <netinet/in.h>
#include <chrono>
#include <vector>
#include <memory>
#include <ctime>
#include <sys/time.h>

class Session {
public:
    Session(int controlSocket, int testSocket);
    ~Session();
    
    void run();
    bool isExpired() const;
    bool matchesTestAddress(const struct sockaddr_in& addr) const;
    void processTestPacket(const char* data, size_t size, const struct sockaddr_in& fromAddr);
    
private:
    void handleSetUpResponse();
    void handleServerStart();
    void handleRequestSession();
    void handleAcceptSession();
    void handleStartSessions();
    void handleStopSessions();
    void handleFetchSession();
    
    int controlSocket_;
    int testSocket_;
    std::chrono::steady_clock::time_point lastActivity_;
    
    struct sockaddr_in testClientAddr_;
    uint32_t sid_;
    bool testActive_;
    
    std::vector<char> generateReflectorPacket(const char* data, size_t size, const sockaddr_in& fromAddr);
    void sendControlMessage(const std::vector<char>& message);
    std::vector<char> receiveControlMessage(size_t expectedSize);
};

#endif // TWAMP_SESSION_H