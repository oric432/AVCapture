#pragma once

#include "types.hpp"

namespace VSCapture::Utils {

// Returns the IPv4 address of the primary outbound interface.
// Opens a UDP socket and "connects" to a public address (no packets are sent);
// the OS fills in the local endpoint via the routing table.
inline std::string local_ip() {
    try {
        asio::io_context ctx;
        asio::ip::udp::socket sock(ctx, asio::ip::udp::v4());
        sock.connect(asio::ip::udp::endpoint(
            asio::ip::make_address("8.8.8.8"), 53));
        return sock.local_endpoint().address().to_string();
    } catch (...) {
        return "unknown";
    }
}

} // namespace VSCapture::Utils
