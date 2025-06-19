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
#include <csignal>
#include <fcntl.h>
#include <errno.h>

Server *Server::instance = nullptr;

void Server::signalHandler(int signum)
{
    if (Server::instance)
    {
        std::cerr << "Received signal " << signum << ", initiating shutdown..." << std::endl;
        Server::instance->stop();
    }
}

Server::Server(const std::string &configFile) : config_(configFile), running_(false), controlSocket_(-1), testSocket_(-1)
{
    if (!config_.load())
    {
        throw std::runtime_error("Failed to load configuration");
    }
    Server::instance = this;
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
}

Server::~Server()
{
    Server::instance = nullptr;
    stop();
}

bool Server::start()
{
    if (!setupControlSocket() || !setupTestSocket())
    {
        return false;
    }

    running_ = true;

    controlThread_ = std::thread(&Server::controlServerThread, this);
    testThread_ = std::thread(&Server::testServerThread, this);
    cleanupThread_ = std::thread(&Server::sessionCleanupThread, this);

    std::cout << "TWAMP Server started on control port " << config_.getInt("control_port", 862)
              << ", test port " << config_.getInt("test_port", 863) << std::endl;

    return true;
}

void Server::stop()
{
    if (!running_) return;
    running_ = false;

    std::cout << "Stopping TWAMP server..." << std::endl;

    // Close sockets to unblock threads
    if (controlSocket_ != -1) {
        shutdown(controlSocket_, SHUT_RDWR);
        close(controlSocket_);
        controlSocket_ = -1;
    }
    if (testSocket_ != -1) {
        shutdown(testSocket_, SHUT_RDWR);
        close(testSocket_);
        testSocket_ = -1;
    }

    // Stop all sessions
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        for (auto &session : activeSessions_) {
            session->requestStop();
        }
    }

    // Join control connection threads
    std::vector<std::thread> controlThreads;
    {
        std::lock_guard<std::mutex> lock(controlConnectionThreadsMutex_);
        controlThreads = std::move(controlConnectionThreads_);
        controlConnectionThreads_.clear();
    }
    for (auto& thread : controlThreads) {
        if (thread.joinable()) thread.join();
    }

    // Join session threads
    {
        std::lock_guard<std::mutex> lock(sessionThreadsMutex_);
        for (auto& thread : sessionThreads_) {
            if (thread.joinable()) thread.join();
        }
        sessionThreads_.clear();
    }

    // Clear sessions
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        activeSessions_.clear();
    }

    // Join main threads
    if (controlThread_.joinable()) controlThread_.join();
    if (testThread_.joinable()) testThread_.join();
    if (cleanupThread_.joinable()) cleanupThread_.join();

    std::cout << "TWAMP server stopped." << std::endl;
}


bool Server::setupControlSocket()
{
    controlSocket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (controlSocket_ < 0)
    {
        std::cerr << "Failed to create control socket" << std::endl;
        return false;
    }

    int enable = 1;
    if (setsockopt(controlSocket_, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0)
    {
        std::cerr << "Failed to set SO_REUSEADDR on control socket" << std::endl;
        close(controlSocket_);
        controlSocket_ = -1;
        return false;
    }

    // Set socket to non-blocking mode to handle shutdown better
    int flags = fcntl(controlSocket_, F_GETFL, 0);
    if (flags == -1 || fcntl(controlSocket_, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        std::cerr << "Failed to set control socket to non-blocking" << std::endl;
        close(controlSocket_);
        controlSocket_ = -1;
        return false;
    }

    memset(&controlAddr_, 0, sizeof(controlAddr_));
    controlAddr_.sin_family = AF_INET;
    controlAddr_.sin_addr.s_addr = htonl(INADDR_ANY);
    controlAddr_.sin_port = htons(config_.getInt("control_port", 862));

    if (bind(controlSocket_, (struct sockaddr *)&controlAddr_, sizeof(controlAddr_)) < 0)
    {
        std::cerr << "Failed to bind control socket: " << strerror(errno) << std::endl;
        close(controlSocket_);
        controlSocket_ = -1;
        return false;
    }

    if (listen(controlSocket_, 10) < 0)
    {
        std::cerr << "Failed to listen on control socket: " << strerror(errno) << std::endl;
        close(controlSocket_);
        controlSocket_ = -1;
        return false;
    }

    return true;
}

bool Server::setupTestSocket()
{
    testSocket_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (testSocket_ < 0)
    {
        std::cerr << "Failed to create test socket" << std::endl;
        return false;
    }

    // Set socket to non-blocking mode
    int flags = fcntl(testSocket_, F_GETFL, 0);
    if (flags == -1 || fcntl(testSocket_, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        std::cerr << "Failed to set test socket to non-blocking" << std::endl;
        close(testSocket_);
        testSocket_ = -1;
        return false;
    }

    memset(&testAddr_, 0, sizeof(testAddr_));
    testAddr_.sin_family = AF_INET;
    testAddr_.sin_addr.s_addr = htonl(INADDR_ANY);
    testAddr_.sin_port = htons(config_.getInt("test_port", 863));

    if (bind(testSocket_, (struct sockaddr *)&testAddr_, sizeof(testAddr_)) < 0)
    {
        std::cerr << "Failed to bind test socket: " << strerror(errno) << std::endl;
        close(testSocket_);
        testSocket_ = -1;
        return false;
    }

    return true;
}

void Server::controlServerThread()
{
    while (running_)
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(controlSocket_, &readfds);
        
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int activity = select(controlSocket_ + 1, &readfds, NULL, NULL, &timeout);
        
        if (!running_) break;
        
        if (activity < 0)
        {
            if (errno == EINTR) continue;
            std::cerr << "Select error on control socket: " << strerror(errno) << std::endl;
            break;
        }
        
        if (activity == 0) continue; // Timeout
        
        if (FD_ISSET(controlSocket_, &readfds))
        {
            struct sockaddr_in clientAddr;
            socklen_t clientAddrLen = sizeof(clientAddr);
            int clientSocket = accept(controlSocket_, (struct sockaddr *)&clientAddr, &clientAddrLen);

            if (clientSocket < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                if (errno == EINTR) continue;
                if (running_)
                {
                    std::cerr << "Failed to accept control connection: " << strerror(errno) << std::endl;
                }
                continue;
            }

            // Set client socket back to blocking mode for session handling
            int flags = fcntl(clientSocket, F_GETFL, 0);
            if (flags != -1) {
                fcntl(clientSocket, F_SETFL, flags & ~O_NONBLOCK);
            }

            std::cout << "New control connection from " << inet_ntoa(clientAddr.sin_addr) << std::endl;
            std::thread t(&Server::handleControlConnection, this, clientSocket);
            {
                std::lock_guard<std::mutex> lock(controlConnectionThreadsMutex_);
                controlConnectionThreads_.push_back(std::move(t));
            }
        }
    }
}

void Server::testServerThread()
{
    const int bufferSize = 1024;
    char buffer[bufferSize];

    while (running_)
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(testSocket_, &readfds);
        
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int activity = select(testSocket_ + 1, &readfds, NULL, NULL, &timeout);
        
        if (!running_) break;
        
        if (activity < 0)
        {
            if (errno == EINTR) continue;
            std::cerr << "Select error on test socket: " << strerror(errno) << std::endl;
            break;
        }
        
        if (activity == 0) continue; // Timeout
        
        if (FD_ISSET(testSocket_, &readfds))
        {
            struct sockaddr_in clientAddr;
            socklen_t clientAddrLen = sizeof(clientAddr);

            ssize_t bytesRead = recvfrom(testSocket_, buffer, bufferSize, 0,
                                         (struct sockaddr *)&clientAddr, &clientAddrLen);

            if (bytesRead < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                if (errno == EINTR) continue;
                if (running_)
                {
                    std::cerr << "Failed to receive test packet: " << strerror(errno) << std::endl;
                }
                continue;
            }

            // Process test packet
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            for (auto &session : activeSessions_)
            {
                if (session->matchesTestAddress(clientAddr))
                {
                    session->processTestPacket(buffer, bytesRead, clientAddr);
                    break;
                }
            }
        }
    }
}

void Server::sessionCleanupThread()
{
    while (running_)
    {
        for (int i = 0; i < 10; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        if (!running_) break;

        std::lock_guard<std::mutex> lock(sessionsMutex_);
        auto it = activeSessions_.begin();
        while (it != activeSessions_.end())
        {
            if ((*it)->isExpired())
            {
                std::cout << "Cleaning up expired session" << std::endl;
                it = activeSessions_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}

void Server::handleControlConnection(int clientSocket)
{
    try
    {
        // Send server greeting first
        std::vector<char> serverGreeting(12, 0);
        serverGreeting[3] = 1; // Mode 1 (unauthenticated)
        int flags = fcntl(clientSocket, F_GETFL, 0);
        if (flags != -1) fcntl(clientSocket, F_SETFL, flags & ~O_NONBLOCK);

        // Add server identifier (random number)
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint32_t> dis;
        uint32_t serverId = htonl(dis(gen));
        memcpy(&serverGreeting[4], &serverId, sizeof(serverId));

        if (send(clientSocket, serverGreeting.data(), serverGreeting.size(), 0) !=
            static_cast<ssize_t>(serverGreeting.size()))
        {
            throw std::runtime_error("Failed to send server greeting");
        }

        // Receive client greeting
        std::vector<char> clientGreeting(12);
        ssize_t totalReceived = 0;
        while (totalReceived < 12) {
            ssize_t n = recv(clientSocket, clientGreeting.data() + totalReceived, 
                            12 - totalReceived, 0);
            if (n == 0) throw std::runtime_error("Client disconnected");
            if (n < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) 
                    throw std::runtime_error("Client greeting timeout");
                throw std::runtime_error(strerror(errno));
            }
            totalReceived += n;
        }
        if (recv(clientSocket, clientGreeting.data(), clientGreeting.size(), MSG_WAITALL) !=
            static_cast<ssize_t>(clientGreeting.size()))
        {
            throw std::runtime_error("Failed to receive client greeting");
        }

        // Check client mode
        if (clientGreeting[3] != 1)
        {
            throw std::runtime_error("Unsupported client mode");
        }

        std::cout << "Control handshake completed" << std::endl;

        // Create session and add to active sessions
        auto session = std::make_shared<Session>(clientSocket, testSocket_);

        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            activeSessions_.push_back(session);
        }

        // Run session (this will block until session ends)
        std::lock_guard<std::mutex> lock(sessionThreadsMutex_);
        sessionThreads_.emplace_back([session]() {
            try {
                session->run();
            } catch (const std::exception &e) {
                std::cerr << "Session run error: " << e.what() << std::endl;
            }
        });
    }
    catch (const std::exception &e)
    {
        std::cerr << "Control connection error: " << e.what() << std::endl;
        close(clientSocket);
    }
}