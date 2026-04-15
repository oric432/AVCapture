#include "SyncWorkerClient.hpp"

#include "SyncProtocol.hpp"
#include "SyncTime.hpp"
#include "boost/asio/connect.hpp"
#include "boost/asio/post.hpp"
#include "core/MediaRecorder.hpp"

using namespace VSCapture::Sync;
using namespace VSCapture::Error;
using namespace VSCapture;

SyncWorkerClient::SyncWorkerClient(
    asio::io_context& io_ctx,
    std::string host,
    unsigned short port,
    std::shared_ptr<Core::MediaRecorder> media_recorder)
    : io_ctx_(io_ctx)
    , resolver_(io_ctx)
    , socket_(io_ctx)
    , host_(std::move(host))
    , port_(port)
    , cmd_timer_(io_ctx)
    , reconnect_timer_(io_ctx)
    , ping_timer_(io_ctx)
    , media_recorder_(std::move(media_recorder)) {}

void SyncWorkerClient::start() {
    asio::post(io_ctx_, [this] {
        running_ = true;
        do_connect();
    });
}

void SyncWorkerClient::stop() {
    asio::post(io_ctx_, [this] {
        running_ = false;
        close();
    });
}

void SyncWorkerClient::send(json::object msg) {
    auto buf = std::make_shared<std::string>(to_line(std::move(msg)));
    asio::post(io_ctx_, [this, buf] {
        if (!connected_) {
            return;
        }

        const bool idle = outq_.empty();
        outq_.push_back(buf);

        if (idle) {
            do_write();
        }
    });
}

void SyncWorkerClient::do_connect() {
    if (!running_ || connected_) {
        return;
    }

    boost::system::error_code errc;
    errc = socket_.close(errc);
    resolver_.cancel();

    resolver_.async_resolve(
        host_,
        std::to_string(port_),
        [this](boost::system::error_code errc, const tcp::resolver::results_type& endpoints) {
            if (!running_) {
                return;
            }

            if (errc) {
                Log::sync()->warn("Sync resolve failed ({}:{}): {}", host_, port_, errc.message());
                schedule_reconnect();
                return;
            }

            asio::async_connect(
                socket_,
                endpoints,
                [this](boost::system::error_code connect_errc, const tcp::endpoint& endpoint) {
                    if (!running_) {
                        return;
                    }

                    if (connect_errc) {
                        Log::sync()->warn("Sync connect failed ({}:{}): {}", host_, port_, connect_errc.message());
                        close();
                        schedule_reconnect();
                        return;
                    }

                    connected_ = true;
                    best_offset_ns_ = 0;
                    best_rtt_ns_ = std::numeric_limits<int64_t>::max();

                    Log::sync()->info("Connected to endpoint: {}:{}", endpoint.address().to_string(), endpoint.port());

                    schedule_ping();
                    do_read();
                });
        });
}

void SyncWorkerClient::do_read() {
    asio::async_read_until(
        socket_,
        asio::dynamic_buffer(in_),
        '\n',
        [this](boost::system::error_code errc, std::size_t bytes) {
            if (errc) {
                Log::sync()->warn("Sync read error: {}", errc.message());
                close();
                schedule_reconnect();
                return;
            }

            std::string line = in_.substr(0, bytes);
            in_.erase(0, bytes);
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
                line.pop_back();
            }

            handle_line(line);
            do_read();
        });
}

void SyncWorkerClient::do_write() {
    if (outq_.empty()) {
        return;
    }

    auto keep = outq_.front();
    asio::async_write(
        socket_,
        asio::buffer(*keep),
        [this, keep](boost::system::error_code errc, std::size_t /* bytes */) {
            if (errc) {
                Log::sync()->warn("Sync write error: {}", errc.message());
                close();
                schedule_reconnect();
                return;
            }

            outq_.pop_front();
            if (!outq_.empty()) {
                do_write();
            }
        });
}

void SyncWorkerClient::handle_line(const std::string& line) {
    boost::system::error_code jec;
    json::object obj;

    if (!parse_line(line, obj, jec)) {
        return;
    }

    auto* type = obj.if_contains("type");
    if ((type == nullptr) || !type->is_string()) {
        return;
    }

    const auto type_str = type->as_string();

    if (type_str == "pong") {
        auto* t1_val = obj.if_contains("t1");
        auto* t2r_val = obj.if_contains("t2r");
        if ((t1_val == nullptr) || !t1_val->is_int64() || (t2r_val == nullptr) || !t2r_val->is_int64()) {
            return;
        }

        const int64_t t_1 = t1_val->as_int64();
        const auto t2r = t2r_val->as_int64();
        const auto t_3 = system_clock_now_ns();

        const int64_t rtt = t_3 - t_1;
        const int64_t offset = t2r - (t_1 + rtt / 2);

        if (rtt < best_rtt_ns_) {
            best_rtt_ns_ = rtt;
            best_offset_ns_ = offset;
        }

        return;
    }

    if (type_str == "start_at") {
        auto* t0_val = obj.if_contains("t0");

        if (t0_val == nullptr || !t0_val->is_int64()) {
            return;
        }

        const int64_t t0_master = t0_val->as_int64();

        const int64_t t0_local = t0_master - best_offset_ns_;

        cmd_timer_.expires_at(to_steady_time_point(t0_local));
        cmd_timer_.async_wait(boost::asio::bind_executor(io_ctx_, [this](boost::system::error_code errc) {
            if (errc) {
                return;
            }

            if (!media_recorder_) {
                return;
            }

            if (media_recorder_->is_recording()) {
                return;
            }

            if (auto res = media_recorder_->start(); !res) {
                Log::sync()->warn("Failed starting media recorder from sync command: {}", res.error().what());
                return;
            }

            Log::sync()->info("Media recorder started by sync command");
        }));

        return;
    }


    if (type_str == "stop_at") {
        auto* t_val = obj.if_contains("t");

        if (t_val == nullptr || !t_val->is_int64()) {
            return;
        }

        const int64_t t_master = t_val->as_int64();
        const int64_t t_local = t_master - best_offset_ns_;

        cmd_timer_.expires_at(to_steady_time_point(t_local));
        cmd_timer_.async_wait(boost::asio::bind_executor(io_ctx_, [this](boost::system::error_code errc) {
            if (errc) {
                return;
            }

            if (!media_recorder_) {
                return;
            }

            media_recorder_->stop();

            Log::sync()->info("Media recorder stopped by sync command");
        }));

        return;
    }

    if (type_str == "export_at") {
        auto* t_val = obj.if_contains("t");
        auto* output_val = obj.if_contains("output_path");

        if (t_val == nullptr || !t_val->is_int64() || output_val == nullptr || !output_val->is_string())  {
            return;
        }

        const auto t_master = t_val->as_int64();
        const auto t_local = t_master - best_offset_ns_;
        const auto output_path = output_val->as_string();

        cmd_timer_.expires_at(to_steady_time_point(t_local));
        cmd_timer_.async_wait(
            boost::asio::bind_executor(io_ctx_, [this, output_path](boost::system::error_code errc) {
                if (errc) {
                    return;
                }

                if (!media_recorder_) {
                    return;
                }

                if (auto res = media_recorder_->save_and_upload_async(); !res) {
                    Log::sync()->warn("Failed save and upload from sync command: {}", res.error().what());
                    return;
                }

                Log::sync()->info("Media recorder save and upload command succeeded");
            }));

        return;
    }
}

void SyncWorkerClient::schedule_reconnect() {
    if (!running_ || connected_) {
        return;
    }

    reconnect_timer_.expires_after(std::chrono::seconds(1));
    reconnect_timer_.async_wait(boost::asio::bind_executor(io_ctx_, [this](boost::system::error_code errc) {
        if (errc || !running_ || connected_) {
            return;
        }

        do_connect();
    }));
}

void SyncWorkerClient::schedule_ping() {
    if (!running_ || connected_) {
        return;
    }

    static constexpr auto kPingIntervalMs = 250;

    ping_timer_.expires_after(std::chrono::milliseconds(kPingIntervalMs));
    ping_timer_.async_wait(boost::asio::bind_executor(io_ctx_, [this](boost::system::error_code errc) {
        if (errc || !running_ || connected_) {
            return;
        }

        send(ping(system_clock_now_ns()));
        schedule_ping();
    }));
}

void SyncWorkerClient::close() {
    connected_ = false;

    cmd_timer_.cancel();
    ping_timer_.cancel();
    reconnect_timer_.cancel();

    socket_.close();

    outq_.clear();
    in_.clear();
}