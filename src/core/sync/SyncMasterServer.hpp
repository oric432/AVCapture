#pragma once

#include "boost/asio/io_context.hpp"
#include "types.hpp"
#include "utils/error.hpp"


#include <boost/beast/core/flat_buffer.hpp>
#include <memory>
#include <string>

namespace VSCapture::Core {
class MediaRecorder;
}

namespace VSCapture::Sync {
class SyncMasterServer {
public:
  SyncMasterServer(asio::io_context &io_ctx, std::string bind_ip,
                   unsigned short port,
                   std::shared_ptr<Core::MediaRecorder> media_recorder);

  struct SplitRecordingNames {
    std::string video_name;
    std::string audio_name;
  };

  Error::VoidResult start();
  void send_start_at(int64_t t0_master_ns);
  SplitRecordingNames send_save_at(int64_t master_ns);

private:
  struct Session;
  void do_accept();
  void on_worker_connected();
  void on_warmup_timer();
  void on_start_timer();

  asio::io_context &io_ctx_;
  tcp::acceptor acceptor_;
  std::string bind_ip_;
  unsigned short port_{};

  std::weak_ptr<Session> worker_;
  std::shared_ptr<Core::MediaRecorder> media_recorder_;

  asio::steady_timer timer_;
};
} // namespace VSCapture::Sync