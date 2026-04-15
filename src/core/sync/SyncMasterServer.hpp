#pragma once

#include "types.hpp"
#include "utils/error.hpp"
#include "boost/asio/io_context.hpp"

#include <boost/beast/core/flat_buffer.hpp>
#include <memory>
#include <string>

namespace VSCapture::Core {
    class MediaRecorder;
}

namespace VSCapture::Sync {
class SyncMasterServer {
public:
    SyncMasterServer(asio::io_context& io_ctx, std::string bind_ip, unsigned short port,
                     std::shared_ptr<Core::MediaRecorder> media_recorder, double seg_seconds);

    Error::VoidResult start();
    void send_start_at(int64_t t0_master_ns, int seg_ms);
    void send_stop_at(int64_t t_master_ns);
    void send_export_at(int64_t t_master_ns, const std::string& output_path);

private:
    struct Session;
    void do_accept();
    void on_worker_connected();
    void on_warmup_timer();
    void on_start_timer();

    asio::io_context& io_ctx_;
    tcp::acceptor acceptor_;
    std::string bind_ip_;
    unsigned short port_{};

    std::weak_ptr<Session> worker_;
    std::shared_ptr<Core::MediaRecorder> media_recorder_;
    double seg_seconds_ = 0.0;

    asio::steady_timer timer_;
};
} // namespace VSCapture::Sync