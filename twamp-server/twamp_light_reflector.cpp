// twamp_light_reflector.cpp — исправленный код с учётом RFC 5357

#include <iostream>
#include <boost/asio.hpp>
#include <array>
#include <chrono>
#include <cstring>

using boost::asio::ip::udp;
using namespace std::chrono;

uint64_t getNtpTimestamp() {
    auto now = system_clock::now().time_since_epoch();
    auto seconds_part = duration_cast<seconds>(now).count();
    auto fractional_part = duration_cast<nanoseconds>(now).count() % 1'000'000'000ULL;

    uint64_t ntp_seconds = seconds_part + 2208988800ULL;
    uint64_t ntp_fraction = (fractional_part * ((1ULL << 32) / 1'000'000'000ULL));

    return (ntp_seconds << 32) | ntp_fraction;
}

#pragma pack(push, 1)
struct TwampTestPacket {
    uint32_t sequence_number;
    uint64_t timestamp;
    uint16_t error_estimate;
    uint16_t mbz;
    // Остальное может быть заполнением
};

struct TwampTestResponse {
    uint32_t sequence_number;

    uint64_t timestamp;
    uint16_t error_estimate;
    uint16_t mbz1;

    uint64_t receive_timestamp;
    uint32_t sender_sequence_number;

    uint64_t sender_timestamp;
    uint16_t sender_error_estimate;
    uint16_t mbz2;

    uint8_t sender_ttl;
    uint8_t mbz3[15];

    uint8_t hmac[16];  // Пустой, если без аутентификации
    uint8_t padding[0];
};
#pragma pack(pop)

int main() {
    try {
        boost::asio::io_context io_context;
        udp::socket socket(io_context, udp::endpoint(udp::v4(), 862));

        std::cout << "TWAMP Light Reflector запущен на порту 862...\n";

        while (true) {
            std::array<char, 1500> recv_buf{};
            udp::endpoint remote_endpoint;
            boost::system::error_code error;

            size_t len = socket.receive_from(boost::asio::buffer(recv_buf), remote_endpoint, 0, error);

            if (error && error != boost::asio::error::message_size)
                throw boost::system::system_error(error);

            if (len < sizeof(TwampTestPacket)) {
                std::cerr << "Received too small packet." << std::endl;
                continue;
            }

            TwampTestPacket* req = reinterpret_cast<TwampTestPacket*>(recv_buf.data());
            TwampTestResponse resp{};

            resp.sequence_number = htonl(req->sequence_number);
            resp.timestamp = htobe64(getNtpTimestamp());
            resp.error_estimate = htons(0);
            resp.mbz1 = 0;

            resp.receive_timestamp = htobe64(getNtpTimestamp());
            resp.sender_sequence_number = htonl(req->sequence_number);

            resp.sender_timestamp = req->timestamp; // уже в NTP-формате
            resp.sender_error_estimate = req->error_estimate;
            resp.mbz2 = 0;

            resp.sender_ttl = 255; // можно получить от IP_RECVTTL, если нужно
            std::memset(resp.mbz3, 0, sizeof(resp.mbz3));
            std::memset(resp.hmac, 0, sizeof(resp.hmac));

            socket.send_to(boost::asio::buffer(&resp, sizeof(resp)), remote_endpoint);
        }
    } catch (std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << std::endl;
    }

    return 0;
}

