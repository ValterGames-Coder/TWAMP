#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>

// TWAMP Protocol Constants (RFC 5357)
const uint16_t TWAMP_CONTROL_PORT = 862;
const uint32_t TWAMP_VERSION = 1;
const uint32_t TWAMP_ACCEPT = 0;
const uint32_t TWAMP_REJECT = 1;

// TWAMP Message Types
enum TWAMPMessageType {
    REQUEST_TW_SESSION = 5,
    ACCEPT_SESSION = 6,
    START_SESSIONS = 2,
    STOP_SESSIONS = 3,
    REQUEST_SESSION = 1,
    ACCEPT_TW_SESSION = 6
};

// TWAMP Control Message Structures
struct TWAMPServerGreeting {
    uint32_t unused[3];
    uint32_t modes;
    uint32_t challenge[4];
    uint32_t salt[4];
    uint32_t count;
    uint32_t mbz[3];
} __attribute__((packed));

struct TWAMPSetupResponse {
    uint32_t mode;
    uint32_t keyid[20];
    uint32_t token[16];
    uint32_t client_iv[4];
} __attribute__((packed));

struct TWAMPRequestTWSession {
    uint8_t type;
    uint8_t ipvn;
    uint8_t conf_sender;
    uint8_t conf_receiver;
    uint32_t slots;
    uint32_t packets;
    uint16_t sender_port;
    uint16_t receiver_port;
    uint32_t sender_address[4];
    uint32_t receiver_address[4];
    uint32_t sid[4];
    uint32_t padding_length;
    uint32_t start_time[2];
    uint32_t timeout[2];
    uint32_t type_p_descriptor;
    uint32_t mbz[2];
    uint8_t hmac[16];
} __attribute__((packed));

struct TWAMPAcceptSession {
    uint8_t accept;
    uint8_t mbz;
    uint16_t port;
    uint32_t sid[4];
    uint32_t mbz2[3];
    uint8_t hmac[16];
} __attribute__((packed));

struct TWAMPTestPacket {
    uint32_t seq_number;
    uint32_t timestamp[2];
    uint16_t error_estimate;
    uint16_t mbz;
    uint64_t receive_timestamp;
    uint32_t sender_seq_number;
    uint32_t sender_timestamp[2];
    uint16_t sender_error_estimate;
    uint16_t sender_mbz;
    uint8_t sender_ttl;
    uint8_t padding[27];
} __attribute__((packed));

class TWAMPDaemon {
private:
    std::atomic<bool> running;
    int control_socket;
    std::vector<std::thread> worker_threads;
    std::string config_file;
    bool daemon_mode;
    
    // Configuration
    uint16_t control_port;
    std::string bind_address;
    int max_sessions;
    
public:
    TWAMPDaemon() : running(false), control_socket(-1), daemon_mode(true),
                    control_port(TWAMP_CONTROL_PORT), bind_address("0.0.0.0"),
                    max_sessions(100) {}
    
    ~TWAMPDaemon() {
        stop();
    }
    
    void daemonize() {
        pid_t pid = fork();
        if (pid < 0) {
            syslog(LOG_ERR, "Fork failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
        
        if (pid > 0) {
            exit(EXIT_SUCCESS); // Parent exits
        }
        
        // Child continues
        if (setsid() < 0) {
            syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
        
        // Fork again to ensure we're not session leader
        pid = fork();
        if (pid < 0) {
            syslog(LOG_ERR, "Second fork failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
        
        if (pid > 0) {
            exit(EXIT_SUCCESS);
        }
        
        // Set file permissions
        umask(0);
        
        // Change working directory
        if (chdir("/") < 0) {
            syslog(LOG_ERR, "chdir failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
        
        // Close file descriptors
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        
        // Redirect to /dev/null
        open("/dev/null", O_RDONLY);
        open("/dev/null", O_WRONLY);
        open("/dev/null", O_WRONLY);
    }
    
    void setup_signal_handlers() {
        struct sigaction sa;
        sa.sa_handler = [](int sig) {
            syslog(LOG_INFO, "Received signal %d, shutting down", sig);
            // Set global flag or use a more sophisticated mechanism
            static TWAMPDaemon* instance = nullptr;
            if (instance) {
                instance->stop();
            }
        };
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGQUIT, &sa, nullptr);
        
        // Ignore SIGPIPE
        signal(SIGPIPE, SIG_IGN);
    }
    
    bool create_control_socket() {
        control_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (control_socket < 0) {
            syslog(LOG_ERR, "Failed to create control socket: %s", strerror(errno));
            return false;
        }
        
        int opt = 1;
        if (setsockopt(control_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            syslog(LOG_ERR, "Failed to set SO_REUSEADDR: %s", strerror(errno));
            close(control_socket);
            return false;
        }
        
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(control_port);
        
        if (inet_pton(AF_INET, bind_address.c_str(), &addr.sin_addr) <= 0) {
            syslog(LOG_ERR, "Invalid bind address: %s", bind_address.c_str());
            close(control_socket);
            return false;
        }
        
        if (bind(control_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            syslog(LOG_ERR, "Failed to bind control socket: %s", strerror(errno));
            close(control_socket);
            return false;
        }
        
        if (listen(control_socket, 10) < 0) {
            syslog(LOG_ERR, "Failed to listen on control socket: %s", strerror(errno));
            close(control_socket);
            return false;
        }
        
        syslog(LOG_INFO, "TWAMP control server listening on %s:%d", 
               bind_address.c_str(), control_port);
        return true;
    }
    
    void handle_control_connection(int client_socket) {
        syslog(LOG_INFO, "New control connection established");
        
        // Send Server Greeting
        TWAMPServerGreeting greeting;
        memset(&greeting, 0, sizeof(greeting));
        greeting.modes = htonl(0); // Unauthenticated mode
        
        if (send(client_socket, &greeting, sizeof(greeting), 0) < 0) {
            syslog(LOG_ERR, "Failed to send server greeting: %s", strerror(errno));
            close(client_socket);
            return;
        }
        
        // Receive Setup Response
        TWAMPSetupResponse setup;
        if (recv(client_socket, &setup, sizeof(setup), 0) != sizeof(setup)) {
            syslog(LOG_ERR, "Failed to receive setup response");
            close(client_socket);
            return;
        }
        
        // Send Server Start (Accept)
        uint32_t accept = htonl(TWAMP_ACCEPT);
        if (send(client_socket, &accept, sizeof(accept), 0) < 0) {
            syslog(LOG_ERR, "Failed to send server start");
            close(client_socket);
            return;
        }
        
        // Handle session requests
        while (running) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(client_socket, &readfds);
            
            struct timeval timeout;
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
            
            int result = select(client_socket + 1, &readfds, nullptr, nullptr, &timeout);
            if (result < 0) {
                if (errno != EINTR) {
                    syslog(LOG_ERR, "Select error: %s", strerror(errno));
                }
                break;
            }
            
            if (result == 0) continue; // Timeout
            
            if (FD_ISSET(client_socket, &readfds)) {
                uint8_t msg_type;
                if (recv(client_socket, &msg_type, 1, MSG_PEEK) <= 0) {
                    break; // Client disconnected
                }
                
                if (msg_type == REQUEST_TW_SESSION) {
                    handle_session_request(client_socket);
                } else if (msg_type == START_SESSIONS) {
                    handle_start_sessions(client_socket);
                } else if (msg_type == STOP_SESSIONS) {
                    handle_stop_sessions(client_socket);
                    break;
                }
            }
        }
        
        close(client_socket);
        syslog(LOG_INFO, "Control connection closed");
    }
    
    void handle_session_request(int client_socket) {
        TWAMPRequestTWSession request;
        if (recv(client_socket, &request, sizeof(request), 0) != sizeof(request)) {
            syslog(LOG_ERR, "Failed to receive session request");
            return;
        }
        
        // Create test session socket
        int test_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (test_socket < 0) {
            syslog(LOG_ERR, "Failed to create test socket: %s", strerror(errno));
            send_session_reject(client_socket);
            return;
        }
        
        struct sockaddr_in test_addr;
        memset(&test_addr, 0, sizeof(test_addr));
        test_addr.sin_family = AF_INET;
        test_addr.sin_addr.s_addr = INADDR_ANY;
        test_addr.sin_port = 0; // Let system choose port
        
        if (bind(test_socket, (struct sockaddr*)&test_addr, sizeof(test_addr)) < 0) {
            syslog(LOG_ERR, "Failed to bind test socket: %s", strerror(errno));
            close(test_socket);
            send_session_reject(client_socket);
            return;
        }
        
        socklen_t addr_len = sizeof(test_addr);
        if (getsockname(test_socket, (struct sockaddr*)&test_addr, &addr_len) < 0) {
            syslog(LOG_ERR, "Failed to get test socket name: %s", strerror(errno));
            close(test_socket);
            send_session_reject(client_socket);
            return;
        }
        
        // Send Accept Session
        TWAMPAcceptSession accept;
        memset(&accept, 0, sizeof(accept));
        accept.accept = 0; // Accept
        accept.port = test_addr.sin_port;
        memcpy(accept.sid, request.sid, sizeof(accept.sid));
        
        if (send(client_socket, &accept, sizeof(accept), 0) < 0) {
            syslog(LOG_ERR, "Failed to send accept session: %s", strerror(errno));
            close(test_socket);
            return;
        }
        
        // Start test session handler in separate thread
        std::thread test_thread([this, test_socket]() {
            handle_test_session(test_socket);
        });
        test_thread.detach();
        
        syslog(LOG_INFO, "Test session created on port %d", ntohs(test_addr.sin_port));
    }
    
    void send_session_reject(int client_socket) {
        TWAMPAcceptSession reject;
        memset(&reject, 0, sizeof(reject));
        reject.accept = 1; // Reject
        send(client_socket, &reject, sizeof(reject), 0);
    }
    
    void handle_start_sessions(int client_socket) {
        uint8_t start_msg[16];
        if (recv(client_socket, start_msg, sizeof(start_msg), 0) != sizeof(start_msg)) {
            syslog(LOG_ERR, "Failed to receive start sessions message");
            return;
        }
        
        // Send Start-Ack
        uint32_t start_ack = htonl(TWAMP_ACCEPT);
        if (send(client_socket, &start_ack, sizeof(start_ack), 0) < 0) {
            syslog(LOG_ERR, "Failed to send start ack: %s", strerror(errno));
        }
        
        syslog(LOG_INFO, "Test sessions started");
    }
    
    void handle_stop_sessions(int client_socket) {
        uint8_t stop_msg[16];
        if (recv(client_socket, stop_msg, sizeof(stop_msg), 0) != sizeof(stop_msg)) {
            syslog(LOG_ERR, "Failed to receive stop sessions message");
            return;
        }
        
        // Send Stop-Ack
        uint32_t stop_ack = htonl(TWAMP_ACCEPT);
        if (send(client_socket, &stop_ack, sizeof(stop_ack), 0) < 0) {
            syslog(LOG_ERR, "Failed to send stop ack: %s", strerror(errno));
        }
        
        syslog(LOG_INFO, "Test sessions stopped");
    }
    
    void handle_test_session(int test_socket) {
        char buffer[1500];
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        while (running) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(test_socket, &readfds);
            
            struct timeval timeout;
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
            
            int result = select(test_socket + 1, &readfds, nullptr, nullptr, &timeout);
            if (result <= 0) continue;
            
            ssize_t bytes = recvfrom(test_socket, buffer, sizeof(buffer), 0,
                                   (struct sockaddr*)&client_addr, &addr_len);
            if (bytes > 0) {
                // Echo the packet back (TWAMP reflector behavior)
                TWAMPTestPacket* packet = (TWAMPTestPacket*)buffer;
                
                // Update receive timestamp
                auto now = std::chrono::high_resolution_clock::now();
                auto timestamp = now.time_since_epoch().count();
                packet->receive_timestamp = htobe64(timestamp);
                
                sendto(test_socket, buffer, bytes, 0,
                       (struct sockaddr*)&client_addr, addr_len);
            }
        }
        
        close(test_socket);
        syslog(LOG_INFO, "Test session closed");
    }
    
    bool start() {
        if (daemon_mode) {
            daemonize();
        }
        
        openlog("twampd", LOG_PID | LOG_CONS, LOG_DAEMON);
        syslog(LOG_INFO, "TWAMP daemon starting");
        
        setup_signal_handlers();
        
        if (!create_control_socket()) {
            return false;
        }
        
        running = true;
        
        while (running) {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(control_socket, &readfds);
            
            struct timeval timeout;
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
            
            int result = select(control_socket + 1, &readfds, nullptr, nullptr, &timeout);
            if (result < 0) {
                if (errno != EINTR) {
                    syslog(LOG_ERR, "Select error on control socket: %s", strerror(errno));
                    break;
                }
                continue;
            }
            
            if (result == 0) continue; // Timeout
            
            if (FD_ISSET(control_socket, &readfds)) {
                int client_socket = accept(control_socket, 
                                         (struct sockaddr*)&client_addr, &addr_len);
                if (client_socket < 0) {
                    if (errno != EINTR) {
                        syslog(LOG_ERR, "Accept error: %s", strerror(errno));
                    }
                    continue;
                }
                
                // Handle connection in separate thread
                std::thread client_thread([this, client_socket]() {
                    handle_control_connection(client_socket);
                });
                client_thread.detach();
            }
        }
        
        return true;
    }
    
    void stop() {
        running = false;
        if (control_socket >= 0) {
            close(control_socket);
            control_socket = -1;
        }
        syslog(LOG_INFO, "TWAMP daemon stopped");
        closelog();
    }
    
    void set_daemon_mode(bool mode) {
        daemon_mode = mode;
    }
    
    void set_config_file(const std::string& file) {
        config_file = file;
    }
};

// Global instance for signal handling
TWAMPDaemon* g_daemon = nullptr;

void signal_handler(int sig) {
    if (g_daemon) {
        g_daemon->stop();
    }
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]\n"
              << "Options:\n"
              << "  -f, --foreground    Run in foreground (don't daemonize)\n"
              << "  -c, --config FILE   Configuration file path\n"
              << "  -h, --help         Show this help message\n"
              << "  -v, --version      Show version information\n";
}

int main(int argc, char* argv[]) {
    TWAMPDaemon daemon;
    g_daemon = &daemon;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-f" || arg == "--foreground") {
            daemon.set_daemon_mode(false);
        } else if (arg == "-c" || arg == "--config") {
            if (i + 1 < argc) {
                daemon.set_config_file(argv[++i]);
            } else {
                std::cerr << "Error: -c requires a file path\n";
                return 1;
            }
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            std::cout << "TWAMP Daemon v1.0 (RFC 5357 compliant)\n";
            return 0;
        } else {
            std::cerr << "Error: Unknown option " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }
    
    // Set up signal handlers
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGQUIT, &sa, nullptr);
    
    // Start the daemon
    if (!daemon.start()) {
        std::cerr << "Failed to start TWAMP daemon\n";
        return 1;
    }
    
    return 0;
}
