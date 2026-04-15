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

  std::deque<std::shared_ptr<std::string>> outq_;

  explicit Session(tcp::socket sock)
      : socket_(std::move(sock)), strand_(sock.get_executor()) {}

  void start() {
    asio::dispatch(strand_, [self = shared_from_this()] { self->do_read(); });
  }

  void close() {
    asio::post(strand_, [self = shared_from_this()] { self->cleanup(); });
  }

  void send(json::object msg) {
    auto buf = std::make_shared<std::string>(to_line(std::move(msg)));
    asio::post(strand_, [self = shared_from_this(), buf] {
      const bool idle = self->outq_.empty();
      self->outq_.push_back(buf);
      if (idle) {
        self->do_write();
      }
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

              std::string line = self->in_.substr(0, bytes);
              self->in_.erase(0, bytes);
              while (!line.empty() &&
                     (line.back() == '\n' || line.back() == '\r')) {
                line.pop_back();
              }

              self->handle_line(line);
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
      const int64_t t_1 =
          (obj.if_contains("t1") != nullptr) ? obj["t1"].as_int64() : 0;
      const int64_t t2r = system_clock_now_ns();
      auto pong_obj = pong(t_1, t2r);
      send(std::move(pong_obj));

      return;
    }
  }

  void do_write() {
    if (outq_.empty()) {
      return;
    }

    auto keep = outq_.front();
    asio::async_write(
        socket_, asio::buffer(*keep),
        asio::bind_executor(strand_, [self = shared_from_this(),
                                      keep](boost::system::error_code errc,
                                            std::size_t /* bytes */) {
          if (errc) {
            self->cleanup();
            return;
          }

          self->outq_.pop_front();
          if (!self->outq_.empty()) {
            self->do_write();
          }
        }));
  }

  void cleanup() {
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
void SyncMasterServer::send_stop_at(int64_t t_master_ns) {
  auto worker = worker_.lock();
  if (!worker) {
    Log::sync()->warn("No sync worker connected; stop at not sent");
    return;
  }

  worker->send(stop_at(t_master_ns));
  timer_.expires_at(Sync::steady_deadline_from_unix_ns(t_master_ns));
  timer_.async_wait([this](const boost::system::error_code & /* errc */) {
    if (!media_recorder_) {
      return;
    }

    if (!media_recorder_->is_recording()) {
      return;
    }

    media_recorder_->stop();

    Log::sync()->info("Audio master stopped audio recorder successfully");
  });
}
void SyncMasterServer::send_export_at(int64_t t_master_ns,
                                      const std::string &output_path) {
  auto worker = worker_.lock();
  if (!worker) {
    Log::sync()->warn("No sync worker connected; export at not sent");
    return;
  }

  worker->send(export_at(t_master_ns, "audio.ts"));

  timer_.expires_at(Sync::steady_deadline_from_unix_ns(t_master_ns));

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

  timer_.expires_at(Sync::steady_deadline_from_unix_ns(scheduled_t0_master_ns));
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
