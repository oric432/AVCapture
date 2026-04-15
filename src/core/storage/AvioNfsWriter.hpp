
#pragma once

#include <memory>
#include <string>
#include "IFileBackend.hpp"

struct AVFormatContext;
struct AVDictionary;
struct AVIOContext;

namespace VSCapture::Nfs {

class AvioNfsWriter {
public:
    AvioNfsWriter() = default;
    ~AvioNfsWriter();

    AvioNfsWriter(const AvioNfsWriter&) = delete;
    AvioNfsWriter& operator=(const AvioNfsWriter&) = delete;

    static Error::Result<std::unique_ptr<AvioNfsWriter>>
    open(IFileBackend& nfs, std::string tmp_path, std::string final_path);

    [[nodiscard]] AVFormatContext* fmt() const noexcept { return muxer_context_; }

    // Call after creating streams + setting codecpar.
    Error::VoidResult write_header(AVDictionary** mux_opts = nullptr);

    // Calls av_write_trailer, flushes/frees AVIO, closes NFS, optional rename tmp->final.
    Error::VoidResult finalize();

private:
    // Owned / managed resources
    IFileBackend* nfs_;
    FileHandle file_{};

    AVFormatContext* muxer_context_{};
    AVIOContext* muxer_io_ctx_{};
    unsigned char* muxer_io_buffer_{};

    // Paths + behavior
    std::string final_path_;
    std::string tmp_path_;
    bool finalized_{false};

    long long pos_{0};

    // Callback entry points
    static int write_cb(void* opaque, const uint8_t* buf, int buf_size);
    static int64_t seek_cb(void* opaque, int64_t offset, int whence);
};

} // namespace VSCapture::Nfs
