// twamp_light_reflector.cpp — RFC 5357 TWAMP Light Reflector с NTP

#include <iostream>
#include <boost/asio.hpp>
#include <array>
#include <chrono>
#include <cstring>
#include <arpa/inet.h>

using boost::asio::ip::udp;

// NTP timestamp: 64 бита = 32 сек + 32 дробных
struct NtpTimestamp {
    uint32_t seconds;
    uint32_t fraction;
};

NtpTimestamp getNtpTimestamp() {
    using namespace std::chrono;

    auto now = system_clock::now().time_since_epoch();
    auto seconds_part = duration_cast<seconds>(now).count();
    auto fraction_part = duration_cast<nanoseconds>(now).count() % 1'000'000'000;

    NtpTimestamp ts;
    ts.seconds = htonl(static_cast<uint32_t>(seconds_part + 2208988800UL)); // UNIX → NTP epoch
    ts.fraction = htonl(static_cast<uint32_t>((fraction_part * (1LL << 32)) / 1'000'000'000));
    return ts;
}

#pragma pack(push, 1)
struct TwampTestPacket {
    uint32_t sequence_number;
    NtpTimestamp timestamp;
    uint16_t error_estimate;
    uint16_t mbz;
    // Остальное не используется
};

struct TwampTestResponse {
    uint32_t sequence_number;
    NtpTimestamp timestamp;
    uint16_t error_estimate;
    uint16_t mbz1;

    NtpTimestamp receive_timestamp;
    uint32_t sender_sequence_number;

    NtpTimestamp sender_timestamp;
    uint16_t sender_error_estimate;
    uint16_t mbz2;

    uint8_t sender_ttl;
    uint8_t mbz3[15];

    uint8_t hmac[16];  // Без HMAC
};
#pragma pack(pop)

int main() {
    try {
        boost::asio::io_context io_context;
        udp::socket socket(io_context, udp::endpoint(udp::v4(), 862));

        std::cout << "TWAMP Light Reflector listening on port 862...\n";

        while (true) {
            std::array<char, 1500> recv_buf{};
            udp::endpoint remote_endpoint;
            boost::system::error_code error;

            size_t len = socket.receive_from(boost::asio::buffer(recv_buf), remote_endpoint, 0, error);
            if (error && error != boost::asio::error::message_size)
                throw boost::system::system_error(error);

            if (len < sizeof(TwampTestPacket)) {
                std::cerr << "Received packet too small." << std::endl;
                continue;
            }

            auto* req = reinterpret_cast<TwampTestPacket*>(recv_buf.data());
            TwampTestResponse resp{};

            resp.sequence_number = req->sequence_number;
            resp.timestamp = getNtpTimestamp();
            resp.error_estimate = htons(0);
            resp.mbz1 = 0;

            resp.receive_timestamp = getNtpTimestamp();
            resp.sender_sequence_number = req->sequence_number;

            resp.sender_timestamp = req->timestamp;
            resp.sender_error_estimate = req->error_estimate;
            resp.mbz2 = 0;

            resp.sender_ttl = 255; // не извлекаем TTL, просто фиксированное значение
            std::memset(resp.mbz3, 0, sizeof(resp.mbz3));
            std::memset(resp.hmac, 0, sizeof(resp.hmac)); // без аутентификации

            socket.send_to(boost::asio::buffer(&resp, sizeof(resp)), remote_endpoint);
        }

    } catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << std::endl;
    }

    return 0;
}

