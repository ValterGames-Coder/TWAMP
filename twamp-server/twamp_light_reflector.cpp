#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>
#include <thread>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>

// TWAMP Light Test Packet format according to RFC 5357
struct TWAMPTestPacket {
    uint32_t sequence_number;    // Sequence number
    uint64_t timestamp;          // Timestamp (NTP format)
    uint16_t error_estimate;     // Error estimate
    uint8_t  mbz[2];            // Must be zero
    uint64_t receive_timestamp;  // Receive timestamp (for reflector response)
    uint32_t sender_sequence;    // Sender sequence number (for reflector response)
    uint64_t sender_timestamp;   // Sender timestamp (for reflector response)
    uint16_t sender_error_est;   // Sender error estimate (for reflector response)
    uint8_t  mbz2[2];           // Must be zero
    uint32_t sender_ttl;        // Sender TTL (for reflector response)
    uint8_t  padding[28];       // Padding to make 64 bytes total
} __attribute__((packed));

class TWAMPLightReflector {
private:
    int socket_fd;
    struct sockaddr_in server_addr;
    bool running;
    uint16_t port;
    
    // Convert system time to NTP timestamp format
    uint64_t getNTPTimestamp() {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        
        // NTP epoch starts Jan 1, 1900, Unix epoch starts Jan 1, 1970
        // Difference is 70 years = 2208988800 seconds
        const uint64_t NTP_EPOCH_OFFSET = 2208988800ULL;
        
        uint64_t seconds = tv.tv_sec + NTP_EPOCH_OFFSET;
        uint64_t fraction = ((uint64_t)tv.tv_usec * (1ULL << 32)) / 1000000;
        
        return (seconds << 32) | fraction;
    }
    
    // Convert network byte order to host byte order for 64-bit values
    uint64_t ntohll(uint64_t value) {
        return ((uint64_t)ntohl(value & 0xFFFFFFFF) << 32) | ntohl(value >> 32);
    }
    
    // Convert host byte order to network byte order for 64-bit values
    uint64_t htonll(uint64_t value) {
        return ((uint64_t)htonl(value & 0xFFFFFFFF) << 32) | htonl(value >> 32);
    }
    
public:
    TWAMPLightReflector(uint16_t listen_port = 862) : port(listen_port), running(false) {
        socket_fd = -1;
    }
    
    ~TWAMPLightReflector() {
        stop();
    }
    
    bool initialize() {
        // Create UDP socket
        socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd < 0) {
            std::cerr << "Error creating socket: " << strerror(errno) << std::endl;
            return false;
        }
        
        // Set socket options for reuse
        int opt = 1;
        if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            std::cerr << "Error setting socket options: " << strerror(errno) << std::endl;
            close(socket_fd);
            return false;
        }
        
        // Configure server address
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port);
        
        // Bind socket
        if (bind(socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "Error binding socket to port " << port << ": " << strerror(errno) << std::endl;
            close(socket_fd);
            return false;
        }
        
        std::cout << "TWAMP Light Reflector initialized on port " << port << std::endl;
        return true;
    }
    
    void processTestPacket(const TWAMPTestPacket& received_packet, 
                          TWAMPTestPacket& response_packet,
                          uint64_t receive_time) {
        
        // Copy the original packet data to response
        response_packet = received_packet;
        
        // Set receive timestamp (when we received the packet)
        response_packet.receive_timestamp = htonll(receive_time);
        
        // Copy sender information from received packet
        response_packet.sender_sequence = received_packet.sequence_number;
        response_packet.sender_timestamp = received_packet.timestamp;
        response_packet.sender_error_est = received_packet.error_estimate;
        
        // Set sender TTL (simplified - would need IP header inspection for real TTL)
        response_packet.sender_ttl = htonl(64);
        
        // Clear MBZ fields
        memset(response_packet.mbz, 0, sizeof(response_packet.mbz));
        memset(response_packet.mbz2, 0, sizeof(response_packet.mbz2));
        
        // Update timestamp to current time for response
        response_packet.timestamp = htonll(getNTPTimestamp());
        
        // Set error estimate (simplified - indicates synchronization quality)
        response_packet.error_estimate = htons(0x0001); // 1 microsecond accuracy
    }
    
    void run() {
        if (socket_fd < 0) {
            std::cerr << "Reflector not initialized!" << std::endl;
            return;
        }
        
        running = true;
        std::cout << "TWAMP Light Reflector started. Waiting for test packets..." << std::endl;
        
        TWAMPTestPacket received_packet;
        TWAMPTestPacket response_packet;
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        
        while (running) {
            // Receive test packet
            ssize_t bytes_received = recvfrom(socket_fd, &received_packet, 
                                            sizeof(received_packet), 0,
                                            (struct sockaddr*)&client_addr, 
                                            &client_addr_len);
            
            if (bytes_received < 0) {
                if (errno == EINTR && !running) {
                    break; // Interrupted by signal
                }
                std::cerr << "Error receiving packet: " << strerror(errno) << std::endl;
                continue;
            }
            
            // Record receive time as soon as possible
            uint64_t receive_time = getNTPTimestamp();
            
            if (bytes_received != sizeof(TWAMPTestPacket)) {
                std::cout << "Received packet with incorrect size: " << bytes_received 
                         << " bytes (expected " << sizeof(TWAMPTestPacket) << ")" << std::endl;
                continue;
            }
            
            // Process the test packet and create response
            processTestPacket(received_packet, response_packet, receive_time);
            
            // Send response back to sender
            ssize_t bytes_sent = sendto(socket_fd, &response_packet, 
                                      sizeof(response_packet), 0,
                                      (struct sockaddr*)&client_addr, 
                                      client_addr_len);
            
            if (bytes_sent < 0) {
                std::cerr << "Error sending response: " << strerror(errno) << std::endl;
                continue;
            }
            
            // Log the transaction
            uint32_t seq_num = ntohl(received_packet.sequence_number);
            std::cout << "Reflected packet from " << inet_ntoa(client_addr.sin_addr) 
                     << ":" << ntohs(client_addr.sin_port) 
                     << " (seq: " << seq_num << ")" << std::endl;
        }
        
        std::cout << "TWAMP Light Reflector stopped." << std::endl;
    }
    
    void stop() {
        running = false;
        if (socket_fd >= 0) {
            close(socket_fd);
            socket_fd = -1;
        }
    }
};

// Global reflector instance for signal handling
TWAMPLightReflector* g_reflector = nullptr;

// Signal handler for graceful shutdown
void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ". Shutting down..." << std::endl;
    if (g_reflector) {
        g_reflector->stop();
    }
}

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [port]" << std::endl;
    std::cout << "Default port is 862 (TWAMP Light standard port)" << std::endl;
}

int main(int argc, char* argv[]) {
    uint16_t port = 862; // Default TWAMP Light port
    
    // Parse command line arguments
    if (argc > 2) {
        printUsage(argv[0]);
        return 1;
    }
    
    if (argc == 2) {
        int user_port = atoi(argv[1]);
        if (user_port <= 0 || user_port > 65535) {
            std::cerr << "Invalid port number: " << argv[1] << std::endl;
            return 1;
        }
        port = static_cast<uint16_t>(user_port);
    }
    
    // Set up signal handlers for graceful shutdown
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // Create and initialize reflector
    TWAMPLightReflector reflector(port);
    g_reflector = &reflector;
    
    if (!reflector.initialize()) {
        std::cerr << "Failed to initialize TWAMP Light Reflector" << std::endl;
        return 1;
    }
    
    // Run the reflector
    reflector.run();
    
    return 0;
}
