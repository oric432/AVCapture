#include "SyncMasterServer.hpp"
#include "SyncProtocol.hpp"
#include "SyncTime.hpp"
#include "core/MediaRecorder.hpp"

using namespace VSCapture::Sync;
using namespace VSCapture::Error;
using namespace VSCapture;

struct SyncMasterServer::Session : std::enable_shared_from_this<Session> {
  tcp::socket socket_;
  asio::strand<asio::any_io_executor> strand_;
  std::string in_;

  std::deque<std::unique_ptr<std::string>> outq_;
  bool writing_ = false;

  explicit Session(tcp::socket sock)
      : socket_(std::move(sock)), strand_(sock.get_executor()) {}

  void start() {
    asio::dispatch(strand_, [self = shared_from_this()] { self->do_read(); });
  }

  void close() {
    asio::post(strand_, [self = shared_from_this()] { self->cleanup(); });
  }

  void send(json::object msg) {
    auto buf = std::make_unique<std::string>(to_line(std::move(msg)));
    asio::post(strand_, [self = shared_from_this(), buf = std::move(buf)]() mutable {
      self->outq_.push_back(std::move(buf));
      self->do_write();
    });
  }

private:
  void do_read() {
    asio::async_read_until(
        socket_, asio::dynamic_buffer(in_), '\n',
        asio::bind_executor(
            (strand_), [self = shared_from_this()](
                           boost::system::error_code errc, std::size_t bytes) {
              if (errc) {
                Log::sync()->warn("Sync worker session read error: {}",
                                  errc.message());
                self->close();
                return;
              }

              self->handle_line(strip_line(self->in_, bytes));
              self->do_read();
            }));
  }

  void handle_line(const std::string &line) {
    boost::system::error_code jec;
    json::object obj;

    if (!parse_line(line, obj, jec)) {
      return;
    }

    auto *type = obj.if_contains("type");
    if ((type == nullptr) || !type->is_string()) {
      return;
    }

    const auto type_str = type->as_string();

    if (type_str == "ping") {
      const int64_t ping_sent_ns =
          (obj.if_contains("ping_sent") != nullptr) ? obj["ping_sent"].as_int64() : 0;
      const int64_t ping_recv_ns = system_clock_now_ns();
      auto pong_obj = pong(ping_sent_ns, ping_recv_ns);
      send(std::move(pong_obj));

      return;
    }
  }

  void do_write() {
    if (outq_.empty() || writing_) {
      return;
    }

    writing_ = true;
    auto msg = std::move(outq_.front());
    outq_.pop_front();

    asio::async_write(
        socket_, asio::buffer(*msg),
        asio::bind_executor(strand_, [self = shared_from_this(),
                                      msg = std::move(msg)](boost::system::error_code errc,
                                            std::size_t /* bytes */) {
          self->writing_ = false;
          if (errc) {
            self->cleanup();
            return;
          }

          self->do_write();
        }));
  }

  void cleanup() {
    writing_ = false;
    socket_.shutdown(tcp::socket::shutdown_both);
    socket_.close();

    outq_.clear();
    in_.clear();
  }
};

SyncMasterServer::SyncMasterServer(
    asio::io_context &io_ctx, std::string bind_ip, unsigned short port,
    std::shared_ptr<Core::MediaRecorder> media_recorder)
    : io_ctx_(io_ctx), acceptor_(io_ctx), bind_ip_(std::move(bind_ip)),
      port_(port), media_recorder_(std::move(media_recorder)),
      timer_(io_ctx) {}

VoidResult SyncMasterServer::start() {
  boost::system::error_code errc;

  const auto addr = asio::ip::make_address(bind_ip_, errc);
  if (errc) {
    return std::unexpected(make_error().with_context("invalid bind ip"));
  }

  tcp::endpoint endpoint(addr, port_);

  errc = acceptor_.open(endpoint.protocol(), errc);
  if (errc) {
    return std::unexpected(make_error().with_context("acceptor open failed"));
  }

  errc = acceptor_.set_option(asio::socket_base::reuse_address(true), errc);
  if (errc) {
    return std::unexpected(make_error().with_context("reuse_address failed"));
  }

  errc = acceptor_.bind(endpoint, errc);
  if (errc) {
    return std::unexpected(make_error().with_context("bind failed"));
  }

  errc = acceptor_.listen(asio::socket_base::max_listen_connections, errc);
  if (errc) {
    return std::unexpected(make_error().with_context("listen failed"));
  }

  Log::sync()->info("SyncMaster listening on {}:{}", bind_ip_, port_);

  do_accept();

  return {};
}

void SyncMasterServer::do_accept() {
  acceptor_.async_accept(
      [this](boost::system::error_code errc, tcp::socket sock) {
        if (!errc) {
          boost::system::error_code ep_ec;
          const auto remote = sock.remote_endpoint(ep_ec);

          if (!ep_ec) {
            Log::sync()->info("Sync worker connected from: {}:{}",
                              remote.address().to_string(), remote.port());
          } else {
            Log::sync()->info("Sync worker connected");
          }

          if (auto prev = worker_.lock()) {
            prev->close();
          }

          auto session = std::make_shared<Session>(std::move(sock));
          worker_ = session;
          session->start();

          on_worker_connected();
        } else {
          Log::sync()->warn("Sync accept error: {}", errc.message());
        }

        if (acceptor_.is_open()) {
          do_accept();
        }
      });
}

void SyncMasterServer::send_start_at(int64_t t0_master_ns) {
  auto worker = worker_.lock();
  if (!worker) {
    Log::sync()->warn("No sync worker connected; start at not sent");
    return;
  }

  worker->send(start_at(t0_master_ns));
}
void SyncMasterServer::send_save_at(int64_t master_ns,
                                    const std::string &output_path) {
  auto worker = worker_.lock();
  if (!worker) {
    Log::sync()->warn("No sync worker connected; save_at not sent");
    return;
  }

  worker->send(save_at(master_ns, "audio.ts"));

  timer_.expires_at(Sync::to_steady_time_point(master_ns));

  timer_.async_wait(
      [this, output_path](const boost::system::error_code & /* errc */) {
        if (!media_recorder_) {
          return;
        }

        if (!media_recorder_->is_recording()) {
          return;
        }

        if (auto res = media_recorder_->save_and_upload_async(); !res) {
          Log::sync()->error(
              "Failed saving and uploading in audio master recorder: {}",
              res.error().what());
          return;
        }

        Log::sync()->info("Audio master saved and uploaded successfully");
      });
}

void SyncMasterServer::on_worker_connected() {
  if (!media_recorder_) {
    return;
  }

  static constexpr auto kPingPongDelayMs = 1500;

  timer_.expires_after(std::chrono::milliseconds(kPingPongDelayMs));
  timer_.async_wait([this](const boost::system::error_code & /* errc */) {
    on_warmup_timer();
  });
}

void SyncMasterServer::on_warmup_timer() {
  if (!media_recorder_) {
    return;
  }

  if (media_recorder_->is_recording()) {
    media_recorder_->stop();
  }

  static constexpr auto kStartDelayNs = 2'000'000'000LL;

  auto scheduled_t0_master_ns = Sync::system_clock_now_ns() + kStartDelayNs;

  Log::sync()->info("Scheduling synchrnoized start at master unix ns={}",
                    scheduled_t0_master_ns);
  send_start_at(scheduled_t0_master_ns);

  timer_.expires_at(Sync::to_steady_time_point(scheduled_t0_master_ns));
  timer_.async_wait([this](const boost::system::error_code & /* errc */) {
    on_start_timer();
  });
}
void SyncMasterServer::on_start_timer() {
  if (!media_recorder_) {
    return;
  }

  if (media_recorder_->is_recording()) {
    return;
  }

  if (auto res = media_recorder_->start(); !res) {
    Log::sync()->error("Failed starting audio master recorder: {}",
                       res.error().what());
    return;
  }

  Log::sync()->info("Audio master recorder started");
}
