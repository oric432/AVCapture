#include "core/storage/AvioNfsWriter.hpp"
#include "utils/utils.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}


using namespace VSCapture;

namespace VSCapture::Nfs {

// AVIO Callbacks
// FFmpeg requires C-style callbacks. We forward directly to libnfs.
// No extra state, no guessing, no bookkeeping.

int AvioNfsWriter::write_cb(void* opaque, const uint8_t* buf, int size) {
    auto* self = static_cast<AvioNfsWriter*>(opaque);

    auto res = self->nfs_->write(self->file_, buf, size);
    if (!res) {
        return AVERROR(EIO);
    }

    return *res;
}

int64_t AvioNfsWriter::seek_cb(void* opaque, int64_t offset, int whence) {
    auto* self = static_cast<AvioNfsWriter*>(opaque);

    auto res = self->nfs_->seek(self->file_, offset, whence);
    if (!res) {
        return AVERROR(ESPIPE); // non-seekable
    }
    return *res;
}


AvioNfsWriter::~AvioNfsWriter() {
    if (muxer_context_ != nullptr) {
        if (muxer_context_->pb != nullptr) {
            avio_flush(muxer_context_->pb);
            avio_context_free(&muxer_context_->pb);
        }
        avformat_free_context(muxer_context_);
    }

    if (muxer_io_buffer_ != nullptr) {
        av_free(muxer_io_buffer_);
    }
}

Error::Result<std::unique_ptr<AvioNfsWriter>>
AvioNfsWriter::open(IFileBackend& nfs, std::string tmp_path, std::string final_path) {
    auto writer = std::make_unique<AvioNfsWriter>();
    writer->nfs_ = &nfs;
    writer->tmp_path_ = std::move(tmp_path);
    writer->final_path_ = std::move(final_path);

    // Ensure remote directory exists
    if (auto dir = Utils::parent_dir(writer->tmp_path_); !dir.empty()) {
        if (auto mk_res = nfs.mkdir_p(dir); !mk_res) {
            return std::unexpected(mk_res.error());
        }
    }

    // Open NFS file
    auto file = nfs.open_write_trunc(writer->tmp_path_);
    if (!file) {
        return std::unexpected(file.error());
    }
    writer->file_ = file.value();

    // Create MP4 muxer
    int res = avformat_alloc_output_context2(&writer->muxer_context_, nullptr, "mp4", nullptr);
    if (res < 0 || (writer->muxer_context_ == nullptr)) {
        return std::unexpected(Error::make_error().with_context("Ffmpeg failed allocating output context"));
    }

    // Allocate AVIO buffer (large to reduce NFS round trips)
    constexpr int kbuffer_size = 1 << 20;
    writer->muxer_io_buffer_ = static_cast<unsigned char*>(av_malloc(kbuffer_size));
    if (writer->muxer_io_buffer_ == nullptr) {
        return std::unexpected(Error::make_error().with_context("av_malloc failed"));
    }

    writer->muxer_io_ctx_ = avio_alloc_context(
        writer->muxer_io_buffer_,
        kbuffer_size,
        1, // writeable
        writer.get(), // opaque
        nullptr,
        &AvioNfsWriter::write_cb,
        &AvioNfsWriter::seek_cb);

    if (writer->muxer_io_ctx_ == nullptr) {
        return std::unexpected(Error::make_error().with_context("av_malloc_context failed"));
    }

    // This is what connects the avio context to the format context
    writer->muxer_context_->pb = writer->muxer_io_ctx_;

    // Tells the fmt context that we used custom callbacks - necesarry
    writer->muxer_context_->flags |= AVFMT_FLAG_CUSTOM_IO;

    return std::move(writer);
}

Error::VoidResult AvioNfsWriter::write_header(AVDictionary** mux_opts) {
    if (muxer_context_ == nullptr) {
        return std::unexpected(Error::make_error().with_context("Writer not initialized"));
    }

    int res = avformat_write_header(muxer_context_, mux_opts);
    if (res < 0) {
        return std::unexpected(Error::make_error().with_context("avformat_write_header failed"));
    }

    return {};
}

// wraps up a finished recording: it writes the FFmpeg trailer, releases all FFmpeg and NFS resources, closes the
// underlying NFS file, and atomically renames the temporary output file to its final name (if requested).
Error::VoidResult AvioNfsWriter::finalize() {
    if (finalized_) {
        return {};
    }

    if (muxer_context_ == nullptr) {
        return std::unexpected(Error::make_error().with_context("Writer not initialized"));
    }

    int res = av_write_trailer(muxer_context_);
    if (res < 0) {
        return std::unexpected(Error::make_error().with_context("av_write_trailer failed"));
    }

    // Tear down FFmpeg side first
    if (muxer_context_->pb != nullptr) {
        avio_flush(muxer_context_->pb);
        avio_context_free(&muxer_context_->pb);
        muxer_context_->pb = nullptr;
    }

    avformat_free_context(muxer_context_);
    muxer_context_ = nullptr;

    if (muxer_io_buffer_ != nullptr) {
        av_free(muxer_io_buffer_);
        muxer_io_buffer_ = nullptr;
    }

    // Close NFS file
    if (auto c_res = nfs_->close(file_); !c_res) {
        return std::unexpected(c_res.error().with_context("Error closing nfs file"));
    }

    // Atomic finalize: rename tmp -> final
    if (!final_path_.empty()) {
        auto r_res = nfs_->rename(tmp_path_, final_path_);
        if (!r_res) {
            return std::unexpected(r_res.error().with_context("Error renaming file"));
        }
    }

    finalized_ = true;
    return {};
}
} // namespace VSCapture::Nfs