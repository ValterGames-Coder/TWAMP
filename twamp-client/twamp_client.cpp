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
#include <vector>
#include <iomanip>

// TWAMP Light Test Packet format (same as reflector)
struct TWAMPTestPacket {
    uint32_t sequence_number;
    uint64_t timestamp;
    uint16_t error_estimate;
    uint8_t  mbz[2];
    uint64_t receive_timestamp;
    uint32_t sender_sequence;
    uint64_t sender_timestamp;
    uint16_t sender_error_est;
    uint8_t  mbz2[2];
    uint32_t sender_ttl;
    uint8_t  padding[28];
} __attribute__((packed));

struct TestResult {
    uint32_t sequence;
    uint64_t send_time;
    uint64_t receive_time;
    uint64_t reflect_time;
    uint64_t return_time;
    double rtt_ms;
    bool received;
};

class TWAMPLightClient {
private:
    int socket_fd;
    struct sockaddr_in server_addr;
    std::string server_ip;
    uint16_t server_port;
    uint32_t sequence_counter;
    std::vector<TestResult> results;
    
    uint64_t getNTPTimestamp() {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        const uint64_t NTP_EPOCH_OFFSET = 2208988800ULL;
        uint64_t seconds = tv.tv_sec + NTP_EPOCH_OFFSET;
        uint64_t fraction = ((uint64_t)tv.tv_usec * (1ULL << 32)) / 1000000;
        return (seconds << 32) | fraction;
    }
    
    uint64_t ntohll(uint64_t value) {
        return ((uint64_t)ntohl(value & 0xFFFFFFFF) << 32) | ntohl(value >> 32);
    }
    
    uint64_t htonll(uint64_t value) {
        return ((uint64_t)htonl(value & 0xFFFFFFFF) << 32) | htonl(value >> 32);
    }
    
    double ntpTimestampToMs(uint64_t ntp_time) {
        uint32_t seconds = (ntp_time >> 32) & 0xFFFFFFFF;
        uint32_t fraction = ntp_time & 0xFFFFFFFF;
        return (double)seconds * 1000.0 + ((double)fraction * 1000.0) / (1ULL << 32);
    }
    
public:
    TWAMPLightClient(const std::string& ip, uint16_t port) 
        : server_ip(ip), server_port(port), sequence_counter(1) {
        socket_fd = -1;
    }
    
    ~TWAMPLightClient() {
        if (socket_fd >= 0) {
            close(socket_fd);
        }
    }
    
    bool initialize() {
        socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd < 0) {
            std::cerr << "Error creating socket: " << strerror(errno) << std::endl;
            return false;
        }
        
        // Set receive timeout
        struct timeval timeout;
        timeout.tv_sec = 5;  // 5 second timeout
        timeout.tv_usec = 0;
        
        if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
            std::cerr << "Error setting socket timeout: " << strerror(errno) << std::endl;
            close(socket_fd);
            return false;
        }
        
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_port);
        
        if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) <= 0) {
            std::cerr << "Invalid IP address: " << server_ip << std::endl;
            close(socket_fd);
            return false;
        }
        
        std::cout << "TWAMP Light Client initialized. Target: " << server_ip << ":" << server_port << std::endl;
        return true;
    }
    
    bool sendTestPacket() {
        TWAMPTestPacket packet;
        memset(&packet, 0, sizeof(packet));
        
        // Fill packet
        packet.sequence_number = htonl(sequence_counter);
        packet.timestamp = htonll(getNTPTimestamp());
        packet.error_estimate = htons(0x0001); // 1 microsecond accuracy
        
        uint64_t send_time = getNTPTimestamp();
        
        ssize_t bytes_sent = sendto(socket_fd, &packet, sizeof(packet), 0,
                                   (struct sockaddr*)&server_addr, sizeof(server_addr));
        
        if (bytes_sent < 0) {
            std::cerr << "Error sending packet: " << strerror(errno) << std::endl;
            return false;
        }
        
        // Wait for response
        TWAMPTestPacket response;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        
        ssize_t bytes_received = recvfrom(socket_fd, &response, sizeof(response), 0,
                                         (struct sockaddr*)&from_addr, &from_len);
        
        uint64_t return_time = getNTPTimestamp();
        
        TestResult result;
        result.sequence = sequence_counter;
        result.send_time = send_time;
        result.return_time = return_time;
        
        if (bytes_received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::cout << "Packet " << sequence_counter << ": TIMEOUT" << std::endl;
            } else {
                std::cout << "Packet " << sequence_counter << ": ERROR - " << strerror(errno) << std::endl;
            }
            result.received = false;
            result.rtt_ms = -1;
        } else if (bytes_received != sizeof(TWAMPTestPacket)) {
            std::cout << "Packet " << sequence_counter << ": Invalid response size" << std::endl;
            result.received = false;
            result.rtt_ms = -1;
        } else {
            result.received = true;
            result.receive_time = ntohll(response.receive_timestamp);
            result.reflect_time = ntohll(response.timestamp);
            
            // Calculate RTT in milliseconds
            double send_ms = ntpTimestampToMs(send_time);
            double return_ms = ntpTimestampToMs(return_time);
            result.rtt_ms = return_ms - send_ms;
            
            uint32_t resp_seq = ntohl(response.sender_sequence);
            
            std::cout << "Packet " << sequence_counter 
                     << ": RTT=" << std::fixed << std::setprecision(3) << result.rtt_ms << "ms"
                     << " (seq=" << resp_seq << ")" << std::endl;
        }
        
        results.push_back(result);
        sequence_counter++;
        return result.received;
    }
    
    void runTest(int count, double interval_sec = 1.0) {
        std::cout << "\nStarting TWAMP Light test..." << std::endl;
        std::cout << "Target: " << server_ip << ":" << server_port << std::endl;
        std::cout << "Packets: " << count << ", Interval: " << interval_sec << "s" << std::endl;
        std::cout << "---" << std::endl;
        
        for (int i = 0; i < count; i++) {
            sendTestPacket();
            
            if (i < count - 1) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(static_cast<int>(interval_sec * 1000))
                );
            }
        }
        
        printStatistics();
    }
    
    void printStatistics() {
        if (results.empty()) return;
        
        int sent = results.size();
        int received = 0;
        double total_rtt = 0;
        double min_rtt = 999999;
        double max_rtt = 0;
        
        for (const auto& result : results) {
            if (result.received) {
                received++;
                total_rtt += result.rtt_ms;
                if (result.rtt_ms < min_rtt) min_rtt = result.rtt_ms;
                if (result.rtt_ms > max_rtt) max_rtt = result.rtt_ms;
            }
        }
        
        std::cout << "\n--- TWAMP Light Test Statistics ---" << std::endl;
        std::cout << "Packets sent: " << sent << std::endl;
        std::cout << "Packets received: " << received << std::endl;
        std::cout << "Packet loss: " << std::fixed << std::setprecision(1) 
                 << (100.0 * (sent - received) / sent) << "%" << std::endl;
        
        if (received > 0) {
            double avg_rtt = total_rtt / received;
            std::cout << "RTT min/avg/max: " << std::fixed << std::setprecision(3)
                     << min_rtt << "/" << avg_rtt << "/" << max_rtt << " ms" << std::endl;
        }
    }
};

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " <server_ip> [server_port] [packet_count] [interval]" << std::endl;
    std::cout << "  server_ip    - IP address of TWAMP Light reflector" << std::endl;
    std::cout << "  server_port  - Port (default: 862)" << std::endl;
    std::cout << "  packet_count - Number of packets to send (default: 10)" << std::endl;
    std::cout << "  interval     - Interval between packets in seconds (default: 1.0)" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program_name << " 127.0.0.1" << std::endl;
    std::cout << "  " << program_name << " 192.168.1.100 862 20 0.5" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }
    
    std::string server_ip = argv[1];
    uint16_t server_port = 862;
    int packet_count = 10;
    double interval = 1.0;
    
    if (argc > 2) {
        server_port = static_cast<uint16_t>(atoi(argv[2]));
        if (server_port == 0) {
            std::cerr << "Invalid port: " << argv[2] << std::endl;
            return 1;
        }
    }
    
    if (argc > 3) {
        packet_count = atoi(argv[3]);
        if (packet_count <= 0) {
            std::cerr << "Invalid packet count: " << argv[3] << std::endl;
            return 1;
        }
    }
    
    if (argc > 4) {
        interval = atof(argv[4]);
        if (interval <= 0) {
            std::cerr << "Invalid interval: " << argv[4] << std::endl;
            return 1;
        }
    }
    
    TWAMPLightClient client(server_ip, server_port);
    
    if (!client.initialize()) {
        std::cerr << "Failed to initialize TWAMP Light Client" << std::endl;
        return 1;
    }
    
    client.runTest(packet_count, interval);
    
    return 0;
}
