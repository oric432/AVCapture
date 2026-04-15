#pragma once
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include "types.hpp"

namespace VSCapture::Core {
    class MediaRecorder;
}

namespace VSCapture::Sync {

class SyncWorkerClient {
public:
    SyncWorkerClient(asio::io_context& io_ctx, std::string host, unsigned short port, std::shared_ptr<Core::MediaRecorder> media_recorder);

    void start();
    void stop();

    void send(json::object msg);

private:
    void do_connect();
    void schedule_reconnect();
    void do_read();
    void do_write();
    void schedule_ping();
    void handle_line(const std::string& line);
    void close();

    asio::io_context& io_ctx_;
    tcp::resolver resolver_;
    tcp::socket socket_;

    std::string host_;
    unsigned short port_;

    std::string in_;
    std::deque<std::shared_ptr<std::string>> outq_;

    asio::steady_timer cmd_timer_;
    asio::steady_timer reconnect_timer_;
    asio::steady_timer ping_timer_;

    std::shared_ptr<Core::MediaRecorder> media_recorder_;

    int64_t best_offset_ns_ = 0;
    int64_t best_rtt_ns_ = std::numeric_limits<int64_t>::max();

    int ping_seq_ = 0;
    bool running_ = false;
    bool connected_ = false;
};

} // namespace VSCapture::Sync