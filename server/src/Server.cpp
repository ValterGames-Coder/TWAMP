#include "Server.h"
#include "Session.h"
#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <random>

Server::Server(const std::string& configFile) : config_(configFile), running_(false) {
    if (!config_.load()) {
        throw std::runtime_error("Failed to load configuration");
    }
}

Server::~Server() {
    stop();
}

bool Server::start() {
    if (!setupControlSocket() || !setupTestSocket()) {
        return false;
    }
    
    running_ = true;
    
    controlThread_ = std::thread(&Server::controlServerThread, this);
    testThread_ = std::thread(&Server::testServerThread, this);
    cleanupThread_ = std::thread(&Server::sessionCleanupThread, this);
    
    return true;
}

void Server::stop() {
    running_ = false;
    
    if (controlSocket_ != -1) {
        shutdown(controlSocket_, SHUT_RDWR);
        close(controlSocket_);
    }
    
    if (testSocket_ != -1) {
        shutdown(testSocket_, SHUT_RDWR);
        close(testSocket_);
    }
    
    if (controlThread_.joinable()) {
        controlThread_.join();
    }
    
    if (testThread_.joinable()) {
        testThread_.join();
    }
    
    if (cleanupThread_.joinable()) {
        cleanupThread_.join();
    }
    
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    activeSessions_.clear();
}

std::string Server::generateServerGreeting() const {
    std::string greeting(12, 0);
    
    // First 4 bytes: modes (we support unauthenticated mode only)
    greeting[3] = 1;  // Mode 1 (unauthenticated)
    
    // Next 4 bytes: server identifier (random number)
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis;
    uint32_t serverId = dis(gen);
    
    memcpy(&greeting[4], &serverId, sizeof(serverId));
    
    return greeting;
}

bool Server::setupControlSocket() {
    controlSocket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (controlSocket_ < 0) {
        std::cerr << "Failed to create control socket" << std::endl;
        return false;
    }
    
    int enable = 1;
    if (setsockopt(controlSocket_, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        std::cerr << "Failed to set SO_REUSEADDR on control socket" << std::endl;
        return false;
    }
    
    memset(&controlAddr_, 0, sizeof(controlAddr_));
    controlAddr_.sin_family = AF_INET;
    controlAddr_.sin_addr.s_addr = htonl(INADDR_ANY);
    controlAddr_.sin_port = htons(config_.getInt("control_port", 862));
    
    if (bind(controlSocket_, (struct sockaddr*)&controlAddr_, sizeof(controlAddr_)) < 0) {
        std::cerr << "Failed to bind control socket" << std::endl;
        return false;
    }
    
    if (listen(controlSocket_, 10) < 0) {
        std::cerr << "Failed to listen on control socket" << std::endl;
        return false;
    }
    
    return true;
}

bool Server::setupTestSocket() {
    testSocket_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (testSocket_ < 0) {
        std::cerr << "Failed to create test socket" << std::endl;
        return false;
    }
    
    memset(&testAddr_, 0, sizeof(testAddr_));
    testAddr_.sin_family = AF_INET;
    testAddr_.sin_addr.s_addr = htonl(INADDR_ANY);
    testAddr_.sin_port = htons(config_.getInt("test_port", 863));
    
    if (bind(testSocket_, (struct sockaddr*)&testAddr_, sizeof(testAddr_)) < 0) {
        std::cerr << "Failed to bind test socket" << std::endl;
        return false;
    }
    
    return true;
}

void Server::controlServerThread() {
    while (running_) {
        struct sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);
        int clientSocket = accept(controlSocket_, (struct sockaddr*)&clientAddr, &clientAddrLen);
        
        if (clientSocket < 0) {
            if (running_) {
                std::cerr << "Failed to accept control connection" << std::endl;
            }
            continue;
        }
        
        std::thread(&Server::handleControlConnection, this, clientSocket).detach();
    }
}

void Server::testServerThread() {
    const int bufferSize = 1024;
    char buffer[bufferSize];
    
    while (running_) {
        struct sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);
        
        ssize_t bytesRead = recvfrom(testSocket_, buffer, bufferSize, 0,
                                   (struct sockaddr*)&clientAddr, &clientAddrLen);
        
        if (bytesRead < 0) {
            if (running_) {
                std::cerr << "Failed to receive test packet" << std::endl;
            }
            continue;
        }
        
        // Process test packet
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        for (auto& session : activeSessions_) {
            if (session->matchesTestAddress(clientAddr)) {
                session->processTestPacket(buffer, bytesRead, clientAddr);
                break;
            }
        }
    }
}

void Server::sessionCleanupThread() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        auto it = activeSessions_.begin();
        while (it != activeSessions_.end()) {
            if ((*it)->isExpired()) {
                it = activeSessions_.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void Session::handleRequestSession() {
    try {
        // Принимаем сообщение (27 байт после байта команды)
        auto message = receiveControlMessage(27);
        
        // Полное сообщение = 1 байт команды + 27 байт данных
        std::vector<char> fullMessage(28);
        fullMessage[0] = 1; // Команда
        std::copy(message.begin(), message.end(), fullMessage.begin() + 1);
        
        // Парсим SID
        sid_ = ntohl(*reinterpret_cast<uint32_t*>(&fullMessage[12]));
        
        // Парсим порт и адрес клиента
        testClientAddr_.sin_family = AF_INET;
        testClientAddr_.sin_port = htons(ntohs(*reinterpret_cast<uint16_t*>(&fullMessage[20])));
        testClientAddr_.sin_addr.s_addr = *reinterpret_cast<uint32_t*>(&fullMessage[24]);
        
        // Отправляем Accept-Session
        std::vector<char> acceptMessage(28, 0);
        *reinterpret_cast<uint32_t*>(&acceptMessage[12]) = htonl(sid_);
        acceptMessage[16] = 0; // Accept
        
        sendControlMessage(acceptMessage);
        
    } catch (const std::exception& e) {
        std::cerr << "Request-Session error: " << e.what() << std::endl;
        throw;
    }
}