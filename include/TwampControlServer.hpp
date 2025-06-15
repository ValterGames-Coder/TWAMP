#pragma once
#include <cstdint>

class TwampControlServer {
public:
    explicit TwampControlServer(uint16_t port);
    ~TwampControlServer();
    void run();

private:
    int listen_fd_;
    void acceptLoop();
    void handleClient(int client_fd);
    void negotiateControl(int client_fd);
    void processControlCommands(int client_fd);
};
