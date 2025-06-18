#include "Client.h"
#include <iostream>
#include <string>
#include <cstdlib>

void printUsage() {
    std::cout << "Usage: twamp-client <server-address>[:port] [options]\n"
              << "Options:\n"
              << "  -c <count>    Number of test packets to send (default: 10)\n"
              << "  -i <interval> Interval between packets in ms (default: 1000)\n"
              << "Example:\n"
              << "  twamp-client 192.168.1.1:862 -c 20 -i 500\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        return EXIT_FAILURE;
    }
    
    std::string serverAddress = argv[1];
    int controlPort = 862;
    int testPort = 863;
    
    // Parse port if specified in address
    size_t colonPos = serverAddress.find(':');
    if (colonPos != std::string::npos) {
        controlPort = std::stoi(serverAddress.substr(colonPos + 1));
        serverAddress = serverAddress.substr(0, colonPos);
        testPort = controlPort + 1;
    }
    
    // Parse options
    int packetCount = 10;
    int intervalMs = 1000;
    
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-c" && i + 1 < argc) {
            packetCount = std::stoi(argv[++i]);
        } else if (arg == "-i" && i + 1 < argc) {
            intervalMs = std::stoi(argv[++i]);
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage();
            return EXIT_FAILURE;
        }
    }
    
    try {
        Client client(serverAddress, controlPort, testPort);
        if (!client.runTest(packetCount, intervalMs)) {
            return EXIT_FAILURE;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}