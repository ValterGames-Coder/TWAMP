#pragma once

#include <cstdint>

class SessionReflector {
public:
    explicit SessionReflector(uint16_t port);
    ~SessionReflector();
    void run();

private:
    int udp_fd_;
    uint16_t port_;
    void reflectLoop();
};