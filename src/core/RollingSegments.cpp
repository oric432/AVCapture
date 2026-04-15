#include "RollingSegments.hpp"
#include "video/IScreenRecorder.hpp"

using namespace VSCapture::Core;
using namespace VSCapture;

RollingSegment::~RollingSegment() {
    stop();
}

Error::VoidResult RollingSegment::initialize(
    const Config& cfg,
    Platform::IScreenRecorder* screen_recorder,
    AudioCapturer* audio_capturer) {
    config_ = cfg;
    screen_recorder_ = screen_recorder;
    audio_capturer_ = audio_capturer;

    if ((screen_recorder_ == nullptr) && (audio_capturer_ == nullptr)) {
        return std::unexpected(Error::make_error().with_context("screen_recorder and audio_capturer is null"));
    }

    return {};
}

Error::VoidResult RollingSegment::create_segment_dir() {
    std::error_code errc;
    std::filesystem::create_directories(config_.dir_, errc);
    if (errc) {
        return std::unexpected(
            Error::make_error().with_context(std::format("create directories failed with: {}", errc.message())));
    }
    return {};
}

void RollingSegment::remove_segment_dir() noexcept {
    std::error_code errc;
    std::filesystem::remove_all(config_.dir_, errc);
    if (errc) {
        Log::media_recorder()->warn("Failed to remove temp folder '{}': {}", config_.dir_.string(), errc.message());
    }
}

Error::VoidResult RollingSegment::start() {
    if (running_.exchange(true)) {
        return {};
    }

    if (auto res = create_segment_dir(); !res) {
        running_.store(false);
        return res;
    }

    th_ = std::thread([this] {
        while (running_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::seconds(config_.segment_seconds_));

            if (auto res = tick_save_one_segment(); !res) {
                Log::media_recorder()->error("Segment tick failed: {}", res.error().what());
            }
        }
    });

    return {};
}
void RollingSegment::stop() noexcept {
    if (!running_.exchange(false)) {
        return;
    }

    if (th_.joinable()) {
        th_.join();
    }

    remove_segment_dir();
    seq_.exchange(0, std::memory_order::acq_rel);
}

Error::VoidResult RollingSegment::export_last_segments(size_t segments, const std::filesystem::path& out_mp4) {
    segments = std::min(segments, config_.ring_size_);
    // We use a list.txt since the concat list is very limited
    // and when using a txt file it is more supported by ffmpeg
    auto list_path = config_.dir_ / "list.txt";
    std::ofstream list(list_path);

    const uint64_t cur_seq = seq_.load(std::memory_order::relaxed);
    const uint64_t start_seq = (cur_seq >= segments) ? (cur_seq - segments) : 0;
    Log::media_recorder()->info("cur: {}, start: {}", cur_seq, start_seq);
    for (uint64_t i = start_seq; i < cur_seq; ++i) {
        const auto idx = static_cast<size_t>(i % config_.ring_size_);
        list << "file '" << seg_path(idx).string() << "'\n";
    }
    list.close();

    std::string cmd;

    switch (config_.role_type) {
    case RoleType::kAudio:
        cmd = std::format(
            "ffmpeg -y -hide_banner -loglevel fatal -f concat -safe 0 -i \"{}\" "
            "-c copy -f mpegts \"{}\"",
            list_path.string(),
            out_mp4.string());
        break;
    case RoleType::kVideo:
        cmd = std::format(
            "ffmpeg -y -hide_banner -loglevel fatal -f concat -safe 0 -i \"{}\" "
            "-c copy -f mpegts \"{}\"",
            list_path.string(),
            out_mp4.string());

        break;
    default:
        cmd = std::format(
            "ffmpeg -y -hide_banner -loglevel fatal -f concat -safe 0 -i \"{}\" "
            "-c copy -bsf:v h264_mp4toannexb -bsf:a aac_adtstoasc -movflags +faststart \"{}\"",
            list_path.string(),
            out_mp4.string());

        break;
    }

    if (std::system(cmd.c_str()) != 0) {
        return std::unexpected(Error::make_error().with_context("ffmpeg command failed"));
    }

    std::error_code errc;
    std::filesystem::remove(list_path, errc);
    if (errc) {
        return std::unexpected(Error::make_error().with_context("Failed removing temp list.txt"));
    }

    return {};
}

Error::VoidResult RollingSegment::tick_save_one_segment() {
    const auto idx = static_cast<size_t>(seq_.load(std::memory_order_relaxed) % config_.ring_size_);
    const auto path = seg_path(idx);

    if (audio_capturer_ != nullptr && screen_recorder_ != nullptr) {
        if (auto res = save_av_to_ts(path); !res) {
            return std::unexpected(res.error().with_context("Failed saving current AV buffers to ts file"));
        }
    }
    else if (audio_capturer_ != nullptr) {
        if (auto res = save_a_to_ts(path); !res) {
            return std::unexpected(res.error().with_context("Failed saving current audio buffer to ts file"));
        }
    }
    else {
        if (auto res = save_v_to_ts(path); !res) {
            return std::unexpected(res.error().with_context("Failed saving current video buffer to ts file"));
        }
    }

    if (screen_recorder_ != nullptr) {
        screen_recorder_->clear_frames();
    }
    if (audio_capturer_ != nullptr) {
        audio_capturer_->clear_frames();
    }

    seq_.fetch_add(1, std::memory_order_relaxed);

    Log::media_recorder()->info("Saved segment {} : {}", idx, path.string().c_str());

    return {};
}
std::filesystem::path RollingSegment::seg_path(size_t idx) {
    return config_.dir_ / std::format("seg_{:03}.ts", idx);
}

Error::VoidResult RollingSegment::save_av_to_ts(const std::filesystem::path& ts_path) {
    auto video_frames = screen_recorder_->get_frames();
    auto audio_frames = audio_capturer_->get_frames();

    auto* video_codec_context = screen_recorder_->get_encoder_context();
    auto* audio_codec_context = audio_capturer_->get_encoder_context();

    if (video_frames.empty()) {
        return std::unexpected(Error::make_error().with_context("No video frames to write"));
    }

    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_alloc_output_context2(&fmt_ctx, nullptr, "mpegts", ts_path.string().c_str()) < 0 ||
        (fmt_ctx == nullptr)) {
        return std::unexpected(Error::make_error().with_context("Failed allocating format context"));
    }

    // Step 1: create video stream
    AVStream* video_stream = avformat_new_stream(fmt_ctx, nullptr);
    if (video_stream == nullptr) {
        return std::unexpected(Error::make_error().with_context("Failed to allocate output format context"));
    }

    // Stream ID is the index - video is typically stream 0
    video_stream->id = 0;

    // Copy video encoder setting to the stream
    if (avcodec_parameters_from_context(video_stream->codecpar, video_codec_context) < 0) {
        avformat_free_context(fmt_ctx);
        return std::unexpected(Error::make_error().with_context("Failed to copy video codec parameters"));
    }

    video_stream->time_base = video_codec_context->time_base;

    // let ffmpeg automatically pick the codec tag based on the codec id
    video_stream->codecpar->codec_tag = 0;

    // Step 2: create audio frame
    if (audio_frames.empty()) {
        return std::unexpected(Error::make_error().with_context("No audio frames to write"));
    }

    AVStream* audio_stream = avformat_new_stream(fmt_ctx, nullptr);
    ;
    if (audio_stream == nullptr) {
        return std::unexpected(Error::make_error().with_context("Failed to create audio stream"));
    }

    // Audio is typically stream 1
    audio_stream->id = 1;

    // Copy audio encoder settings
    if (avcodec_parameters_from_context(audio_stream->codecpar, audio_codec_context) < 0) {
        return std::unexpected(Error::make_error().with_context("Failed to copy audio copy parameters"));
    }

    audio_stream->time_base = audio_codec_context->time_base;

    // let ffmpeg automatically pick the codec tag based on the codec id
    audio_stream->codecpar->codec_tag = 0;

    if ((fmt_ctx->oformat->flags & AVFMT_NOFILE) == 0 && (fmt_ctx->pb == nullptr)) {
        auto res = avio_open2(&fmt_ctx->pb, ts_path.string().c_str(), AVIO_FLAG_WRITE, nullptr, nullptr);
        char errbuf[128];
        av_strerror(res, errbuf, sizeof(errbuf));
        if (res < 0) {
            return std::unexpected(
                Error::make_error().with_context(
                    std::format("Failed opening file handle for {}: {}", ts_path.string(), errbuf)));
        }
    }

    // Step 3: write header
    if (avformat_write_header(fmt_ctx, nullptr) < 0) {
        return std::unexpected(Error::make_error().with_context("Failed writing file header"));
    }

    // Step 4: interleave video and audio by timestamp

    size_t video_idx = 0;
    size_t audio_idx = 0;

    int64_t video_frames_diff = video_frames.back().pts_ - static_cast<int64_t>(video_frames.size() - 1);
    int64_t audio_frames_diff =
        audio_frames.back().pts_ - (static_cast<int64_t>(audio_frames.size() - 1) * audio_codec_context->frame_size);

    video_frames_diff = video_frames_diff < 0 ? 0 : video_frames_diff;
    audio_frames_diff = audio_frames_diff < 0 ? 0 : audio_frames_diff;

    Log::media_recorder()->debug(
        "Muxing {} video frames and {} audio frames",
        video_frames.size(),
        audio_frames.size());

    while (video_idx < video_frames.size() && audio_idx < audio_frames.size()) {
        int64_t video_pts = video_frames[video_idx].pts_ - video_frames_diff;
        int64_t audio_pts = audio_frames[audio_idx].pts_ - audio_frames_diff;

        int64_t video_dts = video_frames[video_idx].dts_ - video_frames_diff;
        int64_t audio_dts = audio_frames[audio_idx].dts_ - audio_frames_diff;

        // if video timestamp is earlier than audios then write video
        if (av_compare_ts(video_pts, video_codec_context->time_base, audio_pts, audio_codec_context->time_base) <= 0) {
            const auto& frame = video_frames[video_idx];
            bool is_key_frame = (frame.flags_ & AV_PKT_FLAG_KEY) != 0;
            if (auto res = write_packet_to_stream(
                    fmt_ctx,
                    video_stream,
                    video_codec_context->time_base,
                    frame.data_.data(),
                    frame.data_.size(),
                    video_pts,
                    video_dts,
                    is_key_frame);
                !res) {
                Log::media_recorder()->error(
                    "Failed to write video packet at index {} - {}",
                    video_idx,
                    res.error().what());
                break;
            }

            video_idx++;
        }
        else {
            const auto& frame = audio_frames[audio_idx];
            if (auto res = write_packet_to_stream(
                    fmt_ctx,
                    audio_stream,
                    audio_codec_context->time_base,
                    frame.data_.data(),
                    frame.data_.size(),
                    audio_pts,
                    audio_dts,
                    frame.is_key_frame_);
                !res) {
                Log::media_recorder()->error(
                    "Failed to write audio packet at index {} - {}",
                    video_idx,
                    res.error().what());
                break;
            }

            audio_idx++;
        }
    }

    av_write_trailer(fmt_ctx);
    if ((fmt_ctx->oformat->flags & AVFMT_NOFILE) == 0) {
        avio_closep(&fmt_ctx->pb);
    }
    avformat_free_context(fmt_ctx);

    return {};
}

Error::VoidResult RollingSegment::save_a_to_ts(const std::filesystem::path& ts_path) {
    auto audio_frames = audio_capturer_->get_frames();
    auto* audio_codec_context = audio_capturer_->get_encoder_context();

    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_alloc_output_context2(&fmt_ctx, nullptr, "mpegts", ts_path.string().c_str()) < 0 ||
        (fmt_ctx == nullptr)) {
        return std::unexpected(Error::make_error().with_context("Failed allocating format context"));
    }

    // Step 2: create audio frame
    if (audio_frames.empty()) {
        return std::unexpected(Error::make_error().with_context("No audio frames to write"));
    }

    AVStream* audio_stream = avformat_new_stream(fmt_ctx, nullptr);
    ;
    if (audio_stream == nullptr) {
        return std::unexpected(Error::make_error().with_context("Failed to create audio stream"));
    }

    // Audio is typically stream 1
    audio_stream->id = 1;

    // Copy audio encoder settings
    if (avcodec_parameters_from_context(audio_stream->codecpar, audio_codec_context) < 0) {
        return std::unexpected(Error::make_error().with_context("Failed to copy audio copy parameters"));
    }

    audio_stream->time_base = audio_codec_context->time_base;
    // let ffmpeg automatically pick the codec tag based on the codec id
    audio_stream->codecpar->codec_tag = 0;

    if ((fmt_ctx->oformat->flags & AVFMT_NOFILE) == 0 && (fmt_ctx->pb == nullptr)) {
        auto res = avio_open2(&fmt_ctx->pb, ts_path.string().c_str(), AVIO_FLAG_WRITE, nullptr, nullptr);
        char errbuf[128];
        av_strerror(res, errbuf, sizeof(errbuf));
        if (res < 0) {
            return std::unexpected(
                Error::make_error().with_context(
                    std::format("Failed opening file handle for {}: {}", ts_path.string(), errbuf)));
        }
    }
    // Step 3: write header
    if (avformat_write_header(fmt_ctx, nullptr) < 0) {
        return std::unexpected(Error::make_error().with_context("Failed writing file header"));
    }

    // Step 4: write audio
    int64_t audio_frames_diff =
        audio_frames.back().pts_ - (static_cast<int64_t>(audio_frames.size() - 1) * audio_codec_context->frame_size);

    audio_frames_diff = audio_frames_diff < 0 ? 0 : audio_frames_diff;

    Log::media_recorder()->debug("Muxing {} audio frames", audio_frames.size());

    for (size_t audio_idx = 0; audio_idx < audio_frames.size(); ++audio_idx) {
        int64_t audio_pts = audio_frames[audio_idx].pts_ - audio_frames_diff;
        int64_t audio_dts = audio_frames[audio_idx].dts_ - audio_frames_diff;

        // if video timestamp is earlier than audios then write video
        const auto& frame = audio_frames[audio_idx];
        if (auto res = write_packet_to_stream(
                fmt_ctx,
                audio_stream,
                audio_codec_context->time_base,
                frame.data_.data(),
                frame.data_.size(),
                audio_pts,
                audio_dts,
                frame.is_key_frame_);
            !res) {
            Log::media_recorder()->error(
                "Failed to write audio packet at index {} - {}",
                audio_idx,
                res.error().what());
            break;
        }
    }

    av_write_trailer(fmt_ctx);
    if ((fmt_ctx->oformat->flags & AVFMT_NOFILE) == 0) {
        avio_closep(&fmt_ctx->pb);
    }
    avformat_free_context(fmt_ctx);

    return {};
}

Error::VoidResult RollingSegment::save_v_to_ts(const std::filesystem::path& ts_path) {
    auto video_frames = screen_recorder_->get_frames();

    auto* video_codec_context = screen_recorder_->get_encoder_context();

    if (video_frames.empty()) {
        return std::unexpected(Error::make_error().with_context("No video frames to write"));
    }

    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_alloc_output_context2(&fmt_ctx, nullptr, "mpegts", ts_path.string().c_str()) < 0 ||
        (fmt_ctx == nullptr)) {
        return std::unexpected(Error::make_error().with_context("Failed allocating format context"));
    }

    // Step 1: create video stream
    AVStream* video_stream = avformat_new_stream(fmt_ctx, nullptr);
    if (video_stream == nullptr) {
        return std::unexpected(Error::make_error().with_context("Failed to allocate output format context"));
    }

    // Stream ID is the index - video is typically stream 0
    video_stream->id = 0;

    // Copy video encoder setting to the stream
    if (avcodec_parameters_from_context(video_stream->codecpar, video_codec_context) < 0) {
        avformat_free_context(fmt_ctx);
        return std::unexpected(Error::make_error().with_context("Failed to copy video codec parameters"));
    }

    video_stream->time_base = video_codec_context->time_base;

    // let ffmpeg automatically pick the codec tag based on the codec id
    video_stream->codecpar->codec_tag = 0;

    if ((fmt_ctx->oformat->flags & AVFMT_NOFILE) == 0 && (fmt_ctx->pb == nullptr)) {
        auto res = avio_open2(&fmt_ctx->pb, ts_path.string().c_str(), AVIO_FLAG_WRITE, nullptr, nullptr);
        char errbuf[128];
        av_strerror(res, errbuf, sizeof(errbuf));
        if (res < 0) {
            return std::unexpected(
                Error::make_error().with_context(
                    std::format("Failed opening file handle for {}: {}", ts_path.string(), errbuf)));
        }
    }

    // Step 3: write header
    if (avformat_write_header(fmt_ctx, nullptr) < 0) {
        return std::unexpected(Error::make_error().with_context("Failed writing file header"));
    }

    // Step 4: write video
    int64_t video_frames_diff = video_frames.back().pts_ - static_cast<int64_t>(video_frames.size() - 1);

    video_frames_diff = video_frames_diff < 0 ? 0 : video_frames_diff;

    Log::media_recorder()->debug("Muxing {} video frames", video_frames.size());

    for (size_t video_idx = 0; video_idx < video_frames.size(); ++video_idx) {
        int64_t video_pts = video_frames[video_idx].pts_ - video_frames_diff;
        int64_t video_dts = video_frames[video_idx].dts_ - video_frames_diff;

        const auto& frame = video_frames[video_idx];
        bool is_key_frame = (frame.flags_ & AV_PKT_FLAG_KEY) != 0;
        if (auto res = write_packet_to_stream(
                fmt_ctx,
                video_stream,
                video_codec_context->time_base,
                frame.data_.data(),
                frame.data_.size(),
                video_pts,
                video_dts,
                is_key_frame);
            !res) {
            Log::media_recorder()->error(
                "Failed to write video packet at index {} - {}",
                video_idx,
                res.error().what());
            break;
        }
    }

    av_write_trailer(fmt_ctx);
    if ((fmt_ctx->oformat->flags & AVFMT_NOFILE) == 0) {
        avio_closep(&fmt_ctx->pb);
    }
    avformat_free_context(fmt_ctx);

    return {};
}

Error::VoidResult RollingSegment::write_packet_to_stream(
    AVFormatContext* fmt_ctx,
    AVStream* stream,
    AVRational src_time_base,
    const uint8_t* data,
    size_t size,
    int64_t pts,
    int64_t dts,
    bool is_key_frame) {
    AVPacket* pkt = av_packet_alloc();
    if (pkt == nullptr) {
        return std::unexpected(Error::make_error());
    }

    // Allocate memory for the packet data
    if (av_new_packet(pkt, static_cast<int>(size)) != 0) {
        av_packet_free(&pkt);
        return std::unexpected(Error::make_error());
    }

    // Copy encoded frame data into the packet
    memcpy(pkt->data, data, size);

    // set packet metadata
    pkt->pts = pts;
    pkt->dts = dts;
    pkt->stream_index = stream->index;
    if (is_key_frame) {
        pkt->flags |= AV_PKT_FLAG_KEY;
    }

    // Rescale timestamp from encoder timebase to stream timebase
    av_packet_rescale_ts(pkt, src_time_base, stream->time_base);

    int ret = av_interleaved_write_frame(fmt_ctx, pkt);

    av_packet_free(&pkt);

    if (ret < 0) {
        return std::unexpected(Error::make_error());
    }

    return {};
}