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

Client::Client(const std::string &serverAddress, int controlPort, int testPort, bool shortOutput)
    : serverAddress_(serverAddress), controlPort_(controlPort), testPort_(testPort),
      controlSocket_(-1), testSocket_(-1), sid_(0), shortOutput_(shortOutput) {}

bool Client::runTest(int packetCount, int intervalMs)
{
    try
    {
        if (!connectToServer())
        {
            return false;
        }

        if (!performControlConnection())
        {
            return false;
        }

        if (!setupTestSession())
        {
            return false;
        }

        if (!startTestSession())
        {
            return false;
        }

        if (!sendTestPackets(packetCount, intervalMs))
        {
            return false;
        }

        if (!stopTestSession())
        {
            return false;
        }

        if (controlSocket_ != -1)
        {
            shutdown(controlSocket_, SHUT_RDWR);
            close(controlSocket_);
            controlSocket_ = -1;
        }

        return true;
    }
    catch (const std::exception &e)
    {
        if (!shortOutput_)
        {
            std::cerr << "Error: " << e.what() << std::endl;
        }
        return false;
    }
}

bool Client::connectToServer()
{
    memset(&serverAddr_, 0, sizeof(serverAddr_));
    serverAddr_.sin_family = AF_INET;
    serverAddr_.sin_port = htons(controlPort_);

    if (inet_pton(AF_INET, serverAddress_.c_str(), &serverAddr_.sin_addr) <= 0)
    {
        if (!shortOutput_)
        {
            std::cerr << "Invalid server address" << std::endl;
        }
        return false;
    }

    controlSocket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (controlSocket_ < 0)
    {
        if (!shortOutput_)
        {
            std::cerr << "Failed to create control socket" << std::endl;
        }
        return false;
    }

    // Connect to server
    if (connect(controlSocket_, (struct sockaddr *)&serverAddr_, sizeof(serverAddr_)) < 0)
    {
        if (!shortOutput_)
        {
            std::cerr << "Failed to connect to server" << std::endl;
        }
        return false;
    }

    testSocket_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (testSocket_ < 0)
    {
        if (!shortOutput_)
        {
            std::cerr << "Failed to create test socket" << std::endl;
        }
        return false;
    }

    return true;
}

bool Client::performControlConnection()
{
    try
    {
        // Set socket timeout
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(controlSocket_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(controlSocket_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        // Receive server greeting first (server sends first)
        std::vector<char> serverGreeting(12);
        ssize_t received = recv(controlSocket_, serverGreeting.data(), serverGreeting.size(), MSG_WAITALL);

        if (received != static_cast<ssize_t>(serverGreeting.size()))
        {
            throw std::runtime_error("Failed to receive server greeting");
        }

        // Verify server mode
        if (serverGreeting[3] != 1)
        {
            throw std::runtime_error("Unsupported server mode");
        }

        std::vector<char> clientGreeting(12, 0);
        clientGreeting[3] = 1; // Unauthenticated mode

        if (send(controlSocket_, clientGreeting.data(), clientGreeting.size(), 0) !=
            static_cast<ssize_t>(clientGreeting.size()))
        {
            throw std::runtime_error("Failed to send client greeting");
        }

        if (!shortOutput_)
        {
            std::cout << "Control connection established" << std::endl;
        }
        return true;
    }
    catch (const std::exception &e)
    {
        if (!shortOutput_)
        {
            std::cerr << "Control connection error: " << e.what() << std::endl;
        }
        return false;
    }
}

bool Client::setupTestSession()
{
    try
    {
        struct timeval tv;
        tv.tv_sec = 10;
        tv.tv_usec = 0;
        setsockopt(controlSocket_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(controlSocket_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint32_t> dis;
        sid_ = dis(gen);

        struct sockaddr_in localAddr;
        memset(&localAddr, 0, sizeof(localAddr));
        localAddr.sin_family = AF_INET;
        localAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        localAddr.sin_port = 0; // Let system choose port

        if (bind(testSocket_, (struct sockaddr *)&localAddr, sizeof(localAddr)) < 0)
        {
            if (!shortOutput_)
            {
                std::cerr << "Failed to bind test socket: " << strerror(errno) << std::endl;
            }
            return false;
        }

        socklen_t len = sizeof(localAddr);
        if (getsockname(testSocket_, (struct sockaddr *)&localAddr, &len) < 0)
        {
            if (!shortOutput_)
            {
                std::cerr << "Failed to get socket name: " << strerror(errno) << std::endl;
            }
            return false;
        }

        struct sockaddr_in tempAddr;
        memset(&tempAddr, 0, sizeof(tempAddr));
        tempAddr.sin_family = AF_INET;
        tempAddr.sin_port = htons(1); // Dummy port
        tempAddr.sin_addr = serverAddr_.sin_addr;

        int tempSocket = socket(AF_INET, SOCK_DGRAM, 0);
        if (tempSocket >= 0)
        {
            if (connect(tempSocket, (struct sockaddr *)&tempAddr, sizeof(tempAddr)) == 0)
            {
                socklen_t tempLen = sizeof(tempAddr);
                if (getsockname(tempSocket, (struct sockaddr *)&tempAddr, &tempLen) == 0)
                {
                    localAddr.sin_addr = tempAddr.sin_addr;
                }
            }
            close(tempSocket);
        }

        if (localAddr.sin_addr.s_addr == 0)
        {
            localAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        }

        // Send Request-Session (28 bytes)
        std::vector<char> requestSession(28, 0);
        requestSession[0] = 1; // Request-Session command

        *reinterpret_cast<uint32_t *>(&requestSession[12]) = htonl(sid_);               // SID
        *reinterpret_cast<uint16_t *>(&requestSession[20]) = localAddr.sin_port;        // Keep in network order
        *reinterpret_cast<uint32_t *>(&requestSession[24]) = localAddr.sin_addr.s_addr; // Keep in network order

        ssize_t sent = send(controlSocket_, requestSession.data(), requestSession.size(), 0);
        if (sent != static_cast<ssize_t>(requestSession.size()))
        {
            if (!shortOutput_)
            {
                std::cerr << "Failed to send Request-Session: " << strerror(errno) << std::endl;
            }
            return false;
        }

        if (!shortOutput_)
        {
            std::cout << "Sent Request-Session with SID=" << sid_
                      << ", port=" << ntohs(localAddr.sin_port)
                      << ", addr=" << inet_ntoa(localAddr.sin_addr) << std::endl;
        }

        // Receive Accept-Session (28 bytes)
        std::vector<char> acceptSession(28);
        ssize_t received = recv(controlSocket_, acceptSession.data(), acceptSession.size(), MSG_WAITALL);

        if (received != static_cast<ssize_t>(acceptSession.size()))
        {
            if (!shortOutput_)
            {
                std::cerr << "Failed to receive Accept-Session: "
                          << (received > 0 ? "incomplete" : strerror(errno))
                          << std::endl;
            }
            return false;
        }

        // Check if session was accepted (byte 16 should be 0)
        if (acceptSession[16] != 0)
        {
            if (!shortOutput_)
            {
                std::cerr << "Session was not accepted by server (code: "
                          << static_cast<int>(acceptSession[16]) << ")" << std::endl;
            }
            return false;
        }

        if (!shortOutput_)
        {
            std::cout << "Session accepted by server" << std::endl;
        }
        return true;
    }
    catch (const std::exception &e)
    {
        if (!shortOutput_)
        {
            std::cerr << "Setup test session error: " << e.what() << std::endl;
        }
        return false;
    }
}

bool Client::startTestSession()
{
    // Send Start-Sessions (12 bytes)
    std::vector<char> startSessions(12, 0);
    startSessions[0] = 7; // Start-Sessions command

    if (send(controlSocket_, startSessions.data(), startSessions.size(), 0) !=
        static_cast<ssize_t>(startSessions.size()))
    {
        if (!shortOutput_)
        {
            std::cerr << "Failed to send Start-Sessions" << std::endl;
        }
        return false;
    }

    // Receive Start-Ack (12 bytes)
    std::vector<char> startAck(12);
    if (recv(controlSocket_, startAck.data(), startAck.size(), MSG_WAITALL) !=
        static_cast<ssize_t>(startAck.size()))
    {
        if (!shortOutput_)
        {
            std::cerr << "Failed to receive Start-Ack" << std::endl;
        }
        return false;
    }

    if (!shortOutput_)
    {
        std::cout << "Test session started" << std::endl;
    }
    return true;
}

bool Client::stopTestSession()
{
    // Send Stop-Sessions (12 bytes)
    std::vector<char> stopSessions(12, 0);
    stopSessions[0] = 4; // Stop-Sessions command

    if (send(controlSocket_, stopSessions.data(), stopSessions.size(), 0) !=
        static_cast<ssize_t>(stopSessions.size()))
    {
        if (!shortOutput_)
        {
            std::cerr << "Failed to send Stop-Sessions" << std::endl;
        }
        return false;
    }

    // Receive Stop-Ack (12 bytes)
    std::vector<char> stopAck(12);
    if (recv(controlSocket_, stopAck.data(), stopAck.size(), MSG_WAITALL) !=
        static_cast<ssize_t>(stopAck.size()))
    {
        if (!shortOutput_)
        {
            std::cerr << "Failed to receive Stop-Ack" << std::endl;
        }
        return false;
    }

    if (!shortOutput_)
    {
        std::cout << "Test session stopped" << std::endl;
    }
    return true;
}

bool Client::sendTestPackets(int packetCount, int intervalMs)
{
    // Prepare test packet (64 bytes as per TWAMP specification)
    std::vector<char> testPacket(64, 0);
    double total_rtt = 0;
    double total_out = 0;
    double total_back = 0;
    int successCount = 0;

    struct sockaddr_in testServerAddr;
    memset(&testServerAddr, 0, sizeof(testServerAddr));
    testServerAddr.sin_family = AF_INET;
    testServerAddr.sin_port = htons(testPort_);     // Server's test port
    testServerAddr.sin_addr = serverAddr_.sin_addr; // Server's IP

    if (!shortOutput_)
    {
        std::cout << "Sending " << packetCount << " test packets to "
                  << inet_ntoa(testServerAddr.sin_addr) << ":" << ntohs(testServerAddr.sin_port) << std::endl;
    }

    for (int i = 0; i < packetCount; i++)
    {
        // Fill sequence number (bytes 0-3)
        *reinterpret_cast<uint32_t *>(&testPacket[0]) = htonl(i + 1);

        // Add sender timestamp (bytes 8-15) - TWAMP uses this format
        auto now = std::chrono::system_clock::now();
        auto since_epoch = now.time_since_epoch();
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(since_epoch);
        auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(since_epoch - seconds);

        uint32_t secs = htonl(static_cast<uint32_t>(seconds.count() + 2208988800UL)); // NTP epoch
        uint32_t frac = htonl(static_cast<uint32_t>((microseconds.count() << 32) / 1000000));

        memcpy(&testPacket[8], &secs, 4);  // Sender timestamp seconds
        memcpy(&testPacket[12], &frac, 4); // Sender timestamp fraction

        // Store send time for RTT calculation
        auto send_time = std::chrono::steady_clock::now();

        ssize_t sent = sendto(testSocket_, testPacket.data(), testPacket.size(), 0,
                              (struct sockaddr *)&testServerAddr, sizeof(testServerAddr));

        if (sent != static_cast<ssize_t>(testPacket.size()))
        {
            if (!shortOutput_)
            {
                std::cerr << "Failed to send test packet " << (i + 1) << ": " << strerror(errno) << std::endl;
            }
            return false;
        }

        char response[1024];
        struct sockaddr_in fromAddr;
        socklen_t fromLen = sizeof(fromAddr);

        struct timeval timeout;
        timeout.tv_sec = 2; // Increased timeout
        timeout.tv_usec = 0;
        setsockopt(testSocket_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        ssize_t received = recvfrom(testSocket_, response, sizeof(response), 0,
                                    (struct sockaddr *)&fromAddr, &fromLen);

        if (received > 0)
        {
            auto recv_time = std::chrono::steady_clock::now();
            auto rtt = std::chrono::duration_cast<std::chrono::microseconds>(recv_time - send_time);

            if (received >= 32)
            {
                uint32_t t1_secs, t1_frac;
                uint32_t t2_secs, t2_frac;
                uint32_t t3_secs, t3_frac;

                memcpy(&t1_secs, &response[8], 4);
                memcpy(&t1_frac, &response[12], 4);
                memcpy(&t2_secs, &response[16], 4);
                memcpy(&t2_frac, &response[20], 4);
                memcpy(&t3_secs, &response[24], 4);
                memcpy(&t3_frac, &response[28], 4);

                double T1 = (ntohl(t1_secs) - 2208988800UL) +
                            (static_cast<double>(ntohl(t1_frac)) / 4294967296.0);
                double T2 = (ntohl(t2_secs) - 2208988800UL) +
                            (static_cast<double>(ntohl(t2_frac)) / 4294967296.0);
                double T3 = (ntohl(t3_secs) - 2208988800UL) +
                            (static_cast<double>(ntohl(t3_frac)) / 4294967296.0);

                // Current time T4 in seconds since UNIX epoch
                auto now = std::chrono::system_clock::now();
                auto since_epoch = now.time_since_epoch();
                double T4 = std::chrono::duration_cast<std::chrono::microseconds>(since_epoch).count() / 1e6;

                if (T2 < T1 || T3 < T2 || T4 < T3)
                {
                    if (!shortOutput_)
                    {
                        std::cerr << "Invalid timestamps detected: "
                                  << "T1=" << T1 << ", T2=" << T2
                                  << ", T3=" << T3 << ", T4=" << T4 << std::endl;
                    }
                    continue;
                }
                double out_time = (T2 - T1) * 1000.0;
                double back_time = (T4 - T3) * 1000.0;
                double rtt_calc = (T4 - T1) * 1000.0;

                total_rtt += rtt_calc;
                total_out += out_time;
                total_back += back_time;
                successCount++;

                if (!shortOutput_)
                {
                    std::cout << "Packet " << (i + 1)
                              << " - RTT: " << rtt_calc << " ms"
                              << ", Time Out: " << out_time << " ms"
                              << ", Time Back: " << back_time << " ms"
                              << std::endl;
                }
            }
            else
            {
                if (!shortOutput_)
                {
                    std::cout << "Packet " << (i + 1) << " - Response received (" << received
                              << " bytes), RTT: " << rtt.count() / 1000.0 << " ms" << std::endl;
                }
            }
        }
        else
        {
            if (!shortOutput_)
            {
                std::cout << "Packet " << (i + 1) << " - No response (timeout)" << std::endl;
            }
        }

        if (i < packetCount - 1)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
        }
    }

    if (successCount > 0)
    {
        if (shortOutput_)
        {
            std::cout << "RTT: " << (total_rtt / successCount) << " ms, "
                      << "Time Out: " << (total_out / successCount) << " ms, "
                      << "Time Back: " << (total_back / successCount) << " ms" << std::endl;
        }
        else
        {
            std::cout << "Average RTT: " << (total_rtt / successCount) << " ms" << std::endl;
            std::cout << "Average Time Out: " << (total_out / successCount) << " ms" << std::endl;
            std::cout << "Average Time Back: " << (total_back / successCount) << " ms" << std::endl;
        }
    }

    if (!shortOutput_)
    {
        std::cout << "Test packets completed" << std::endl;
    }
    return true;
}

Client::~Client()
{
    if (controlSocket_ != -1)
    {
        close(controlSocket_);
    }
    if (testSocket_ != -1)
    {
        close(testSocket_);
    }
}