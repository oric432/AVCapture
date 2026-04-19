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
    asio::io_context &io_ctx, std::string host, unsigned short port,
    std::shared_ptr<Core::MediaRecorder> media_recorder)
    : io_ctx_(io_ctx), resolver_(io_ctx), socket_(io_ctx),
      host_(std::move(host)), port_(port), timer_(io_ctx), ping_timer_(io_ctx),
      media_recorder_(std::move(media_recorder)) {}

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
  auto buf = std::make_unique<std::string>(to_line(std::move(msg)));
  asio::post(io_ctx_, [this, buf = std::move(buf)]() mutable {
    if (!connected_) {
      return;
    }

    outq_.push_back(std::move(buf));
    do_write();
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
      host_, std::to_string(port_),
      [this](boost::system::error_code errc,
             const tcp::resolver::results_type &endpoints) {
        if (!running_) {
          return;
        }

        if (errc) {
          Log::sync()->warn("Sync resolve failed ({}:{}): {}", host_, port_,
                            errc.message());
          schedule_reconnect();
          return;
        }

        asio::async_connect(
            socket_, endpoints,
            [this](boost::system::error_code connect_errc,
                   const tcp::endpoint &endpoint) {
              if (!running_) {
                return;
              }

              if (connect_errc) {
                Log::sync()->warn("Sync connect failed ({}:{}): {}", host_,
                                  port_, connect_errc.message());
                schedule_reconnect();
                return;
              }

              connected_ = true;
              best_offset_ns_ = 0;
              best_rtt_ns_ = std::numeric_limits<int64_t>::max();

              Log::sync()->info("Connected to endpoint: {}:{}",
                                endpoint.address().to_string(),
                                endpoint.port());

              schedule_ping();
              do_read();
            });
      });
}

void SyncWorkerClient::do_read() {
  asio::async_read_until(
      socket_, asio::dynamic_buffer(in_), '\n',
      [this](boost::system::error_code errc, std::size_t bytes) {
        if (errc) {
          if (errc != asio::error::eof &&
              errc != asio::error::connection_reset &&
              errc != asio::error::operation_aborted) {
            Log::sync()->warn("Sync read error: {}", errc.message());
          }
          if (media_recorder_ && media_recorder_->is_recording()) {
            media_recorder_->stop();
          }
          schedule_reconnect();
          return;
        }

        handle_line(strip_line(in_, bytes));
        do_read();
      });
}

void SyncWorkerClient::do_write() {
  if (outq_.empty() || writing_) {
    return;
  }

  writing_ = true;
  auto msg = std::move(outq_.front());
  outq_.pop_front();

  asio::async_write(socket_, asio::buffer(*msg),
                    [this, msg = std::move(msg)](boost::system::error_code errc,
                                                 std::size_t /* bytes */) {
                      writing_ = false;
                      if (errc) {
                        if (errc != asio::error::operation_aborted) {
                          Log::sync()->warn("Sync write error: {}",
                                            errc.message());
                        }
                        schedule_reconnect();
                        return;
                      }

                      do_write();
                    });
}

void SyncWorkerClient::handle_line(const std::string &line) {
  boost::system::error_code jec;
  json::object obj;

  if (!parse_line(line, obj, jec)) {
    return;
  }

  const auto *type = get_string(obj, "type");
  if (!type) {
    return;
  }

  if (*type == kTypePong) {
    const auto *ping_sent_ns = get_int64(obj, "ping_sent");
    const auto *ping_recv_ns = get_int64(obj, "ping_recv");
    if (!ping_sent_ns || !ping_recv_ns) {
      return;
    }

    const int64_t pong_recv_ns = system_clock_now_ns();

    // NTP-style offset: assume network is symmetric so ping arrived at
    // master at ping_sent_ns + rtt/2.  offset > 0 means master clock is
    // ahead of ours; subtract offset from master timestamps to get local.
    const int64_t rtt = pong_recv_ns - *ping_sent_ns;
    const int64_t offset = *ping_recv_ns - (*ping_sent_ns + rtt / 2);

    // Keep only the sample with the lowest RTT — high-RTT samples have
    // more asymmetry noise and would worsen the estimate.
    if (rtt < best_rtt_ns_) {
      best_rtt_ns_ = rtt;
      best_offset_ns_ = offset;
    }

    return;
  }

  if (*type == kTypeStartAt) {
    const auto *at_master_ns = get_int64(obj, "at");
    if (!at_master_ns) {
      return;
    }

    const int64_t at_local_ns = *at_master_ns - best_offset_ns_;

    timer_.expires_at(to_steady_time_point(at_local_ns));
    timer_.async_wait(boost::asio::bind_executor(
        io_ctx_, [this](boost::system::error_code errc) {
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
            Log::sync()->warn(
                "Failed starting media recorder from sync command: {}",
                res.error().what());
            return;
          }

          Log::sync()->info("Media recorder started by sync command");
        }));

    return;
  }

  if (*type == kTypeSaveAt) {
    const auto *at_master_ns = get_int64(obj, "at");
    if (!at_master_ns) {
      return;
    }

    const int64_t at_local_ns = *at_master_ns - best_offset_ns_;

    timer_.expires_at(to_steady_time_point(at_local_ns));
    timer_.async_wait(boost::asio::bind_executor(
        io_ctx_, [this](boost::system::error_code errc) {
          if (errc) {
            return;
          }

          if (!media_recorder_) {
            return;
          }

          if (auto res = media_recorder_->save_and_upload_async(); !res) {
            Log::sync()->warn("Failed save and upload from sync command: {}",
                              res.error().what());
            return;
          }

          Log::sync()->info("Media recorder save and upload command succeeded");
        }));

    return;
  }
}

void SyncWorkerClient::schedule_reconnect() {
  close();

  if (!running_) {
    return;
  }

  timer_.expires_after(std::chrono::seconds(1));
  timer_.async_wait(boost::asio::bind_executor(
      io_ctx_, [this](boost::system::error_code errc) {
        if (errc || !running_ || connected_) {
          return;
        }

        do_connect();
      }));
}

void SyncWorkerClient::schedule_ping() {
  if (!running_ || !connected_) {
    return;
  }

  static constexpr auto kPingIntervalMs = 250;

  ping_timer_.expires_after(std::chrono::milliseconds(kPingIntervalMs));
  ping_timer_.async_wait(boost::asio::bind_executor(
      io_ctx_, [this](boost::system::error_code errc) {
        if (errc || !running_ || !connected_) {
          return;
        }

        send(ping(system_clock_now_ns()));
        schedule_ping();
      }));
}

void SyncWorkerClient::close() {
  connected_ = false;
  writing_ = false;

  timer_.cancel();
  ping_timer_.cancel();

  socket_.close();

  outq_.clear();
  in_.clear();
}