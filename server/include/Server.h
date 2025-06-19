#ifndef TWAMP_SERVER_H
#define TWAMP_SERVER_H

#include <netinet/in.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <Config.h>

class Session;

class Server {
public:
    static Server* instance;
    Server(const std::string& configFile);
    ~Server();
    
    bool start();
    void stop();
    
private:
    std::vector<std::thread> controlConnectionThreads_;
    std::mutex controlConnectionThreadsMutex_;
    
    static void signalHandler(int signum);
    void controlServerThread();
    void testServerThread();
    void sessionCleanupThread();
    void handleControlConnection(int clientSocket);
    void handleTestConnection(int clientSocket);
    
    Config config_;
    int controlSocket_;
    int testSocket_;
    std::atomic<bool> running_;
    
    std::thread controlThread_;
    std::thread testThread_;
    std::thread cleanupThread_;
    
    std::mutex sessionsMutex_;
    std::vector<std::shared_ptr<Session>> activeSessions_;
    std::vector<std::thread> sessionThreads_;
    std::mutex sessionThreadsMutex_;
    
    struct sockaddr_in controlAddr_;
    struct sockaddr_in testAddr_;
    
    std::string generateServerGreeting() const;
    bool setupControlSocket();
    bool setupTestSocket();
};

#endif // TWAMP_SERVER_H