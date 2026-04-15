#ifdef _WIN32

    #include <dxgi.h>
    #include <stringapiset.h>
    #include <winerror.h>
    #include <winnls.h>
    #include <winnt.h>

    #include "WinSessionPowerMonitor.hpp"
    #include "ScreenRecorderWindows.hpp"

using namespace VSCapture::Platform;
using namespace VSCapture::Error;
using namespace VSCapture::Log;
using namespace VSCapture;


ScreenRecorderWindows::ScreenRecorderWindows() = default;

ScreenRecorderWindows::~ScreenRecorderWindows() {
    stop();
    cleanup_platform();
}

VoidResult ScreenRecorderWindows::init_platform() {
    if (auto res = create_device(); !res) {
        return res;
    }
    if (auto res = create_desk_duplication(); !res) {
        return res;
    }
    if (auto res = create_staging_texture(); !res) {
        return res;
    }
    if (auto res = init_encoder(); !res) {
        return res;
    }

    d3d_context_->Unmap(staging_texture_.Get(), 0);

    monitor_ = std::make_unique<WinSessionPowerMonitor>();
    if (auto mres = monitor_->start(); !mres) {
        Log::video_capture()->warn("Power moniotr failed: {}", mres.error().what());
        monitor_.reset();
    }

    return {};
}

VoidResult ScreenRecorderWindows::create_device() {
    // 1) --------------------------------------------------------------------
    // Create the D3D11 device and immediate context
    D3D_FEATURE_LEVEL feature_lvl = {};
    HRESULT hres = D3D11CreateDevice(
        nullptr, // Use default adapter
        D3D_DRIVER_TYPE_HARDWARE, // Hardware device
        nullptr, // No software rasterizer
        0, // No flags
        nullptr,
        0, // No feature level array – pick default
        D3D11_SDK_VERSION,
        &d3d_device_,
        &feature_lvl,
        &d3d_context_);

    if (FAILED(hres)) {
        return std::unexpected(make_error().with_context("Failed to initialize D3D11 device"));
    }

    return {};
}

VoidResult ScreenRecorderWindows::create_desk_duplication() {
    // 2) --------------------------------------------------------------------
    // Get the DXGI device that wraps the D3D11 device
    ComPtr<IDXGIDevice> dxgi_device;
    HRESULT hres = d3d_device_.As(&dxgi_device);
    if (FAILED(hres)) {
        return std::unexpected(make_error().with_context("Failed to get DXGI device"));
    }

    // 3) --------------------------------------------------------------------
    // Get the adapter (GPU) that the DXGI device is on
    ComPtr<IDXGIAdapter> dxgi_adapter;
    hres = dxgi_device->GetAdapter(&dxgi_adapter);
    if (FAILED(hres)) {
        return std::unexpected(make_error().with_context("Failed to get DXGI adapter"));
    }

    // 4) --------------------------------------------------------------------
    // Pick the monitor you want to record
    ComPtr<IDXGIOutput> dxgi_output;
    hres = dxgi_adapter->EnumOutputs(config_.monitor_index_, &dxgi_output);
    if (FAILED(hres)) {
        return std::unexpected(
            make_error().with_context(std::format("Failed to enumerate output #{}", config_.monitor_index_)));
    }

    // 5) --------------------------------------------------------------------
    // Get IDXGIOutput1 for duplication
    ComPtr<IDXGIOutput1> dxgi_output1;
    hres = dxgi_output.As(&dxgi_output1);
    if (FAILED(hres)) {
        return std::unexpected(make_error().with_context(std::format("Failed to get IDXGIOutput1")));
    }

    // 6) --------------------------------------------------------------------
    // Create the desktop duplication object
    hres = dxgi_output1->DuplicateOutput(d3d_device_.Get(), &desk_dupl_);
    if (FAILED(hres)) {
        return std::unexpected(make_error().with_context(std::format("Failed duplicating output")));
    }

    // Capture **one** frame right after duplication so we can discover the
    // real desktop resolution that the first frame actually has.
    DXGI_OUTDUPL_FRAME_INFO frame_info;
    ComPtr<IDXGIResource> desktop_resource;

    hres = desk_dupl_->AcquireNextFrame(0, &frame_info, &desktop_resource);
    if (FAILED(hres)) {
        return std::unexpected(make_error().with_context(std::format("Failed to acquire the first frame")));
    }

    // Convert the generic DXGI resource into a D3D11 texture
    ComPtr<ID3D11Texture2D> acquired_texture;
    hres = desktop_resource.As(&acquired_texture);
    if (FAILED(hres)) {
        desk_dupl_->ReleaseFrame();
        return std::unexpected(make_error().with_context(std::format("Failed to convert first frame to a texture")));
    }

    // Get the texture dimensions
    D3D11_TEXTURE2D_DESC first_desc;
    acquired_texture->GetDesc(&first_desc);
    const UINT frame_width = first_desc.Width;
    const UINT frame_height = first_desc.Height;

    // Release the frame – we only needed it to discover the true size
    desk_dupl_->ReleaseFrame();

    // Resolve the capture geometry – output_desc contains the *intended*
    // size, but may be different (e.g. when a monitor was plugged in
    // after the adapter was enumerated).  Use the first frame size if
    // it differs.
    DXGI_OUTPUT_DESC output_desc;
    dxgi_output->GetDesc(&output_desc);
    width_ = output_desc.DesktopCoordinates.right - output_desc.DesktopCoordinates.left;
    height_ = output_desc.DesktopCoordinates.bottom - output_desc.DesktopCoordinates.top;
    rotation_ = get_rotation_type(output_desc.Rotation);

    if (frame_width != static_cast<UINT>(width_) || frame_height != static_cast<UINT>(height_)) {
        Log::video_capture()->debug(
            "Resolution mismatch detected at startup: "
            "expected {}x{} – got {}x{}",
            width_,
            height_,
            frame_width,
            frame_height);

        width_ = frame_width;
        height_ = frame_height;
    }

    return {};
}

VoidResult ScreenRecorderWindows::recreate_desk_duplication() {
    // 2) --------------------------------------------------------------------
    // Get the DXGI device that wraps the D3D11 device
    ComPtr<IDXGIDevice> dxgi_device;
    HRESULT hres = d3d_device_.As(&dxgi_device);
    if (FAILED(hres)) {
        return std::unexpected(make_error().with_context("Failed to get DXGI device"));
    }

    // 3) --------------------------------------------------------------------
    // Get the adapter (GPU) that the DXGI device is on
    ComPtr<IDXGIAdapter> dxgi_adapter;
    hres = dxgi_device->GetAdapter(&dxgi_adapter);
    if (FAILED(hres)) {
        return std::unexpected(make_error().with_context("Failed to get DXGI adapter"));
    }

    // 4) --------------------------------------------------------------------
    // Pick the monitor you want to record
    ComPtr<IDXGIOutput> dxgi_output;
    hres = dxgi_adapter->EnumOutputs(config_.monitor_index_, &dxgi_output);
    if (FAILED(hres)) {
        return std::unexpected(
            make_error().with_context(std::format("Failed to enumerate output #{}", config_.monitor_index_)));
    }

    // 5) --------------------------------------------------------------------
    // Get IDXGIOutput1 for duplication
    ComPtr<IDXGIOutput1> dxgi_output1;
    hres = dxgi_output.As(&dxgi_output1);
    if (FAILED(hres)) {
        return std::unexpected(make_error().with_context(std::format("Failed to get IDXGIOutput1")));
    }

    // 6) --------------------------------------------------------------------
    // Create the desktop duplication object
    hres = dxgi_output1->DuplicateOutput(d3d_device_.Get(), &desk_dupl_);
    if (FAILED(hres)) {
        return std::unexpected(make_error().with_context(std::format("Failed duplicating output")));
    }

    return {};
}
VoidResult ScreenRecorderWindows::create_staging_texture() {
    // Create a CPU‑readable staging texture that matches the *real*
    // desktop resolution.
    D3D11_TEXTURE2D_DESC staging_desc = {};
    staging_desc.Width = width_;
    staging_desc.Height = height_;
    staging_desc.MipLevels = 1;
    staging_desc.ArraySize = 1;
    staging_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    staging_desc.SampleDesc.Count = 1;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    HRESULT hres = d3d_device_->CreateTexture2D(&staging_desc, nullptr, &staging_texture_);
    if (FAILED(hres)) {
        return std::unexpected(make_error().with_context(std::format("Failed to create staging texture")));
    }

    return {};
}
VoidResult ScreenRecorderWindows::init_encoder() {
    // Initialise the encoder & encoder‑queue with the real size
    Core::VideoConfig encoder_cfg{};
    if (rotation_ == Core::RotationType::kRotationRotate90 ||
        rotation_ == Core::RotationType::kRotationRotate270) { // 90° or 270°
        encoder_cfg.width_ = height_;
        encoder_cfg.height_ = width_;
    }
    else {
        encoder_cfg.width_ = width_;
        encoder_cfg.height_ = height_;
    }
    encoder_cfg.fps_ = config_.fps_;
    encoder_cfg.bitrate_ = config_.bitrate_;
    encoder_cfg.rotation_ = rotation_;
    encoder_cfg.buffer_duration_ = config_.buffer_duration_;

    if (auto res = encoder_queue_.initialize(encoder_cfg); !res) {
        return std::unexpected(
            make_error().with_context(std::format("Failed to initialise encoder at startup: {}", res.error().what())));
    }

    return {};
}

VoidResult ScreenRecorderWindows::reinit_duplication() {
    staging_texture_.Reset();
    desk_dupl_.Reset();

    if (auto res = recreate_desk_duplication(); !res) {
        return res;
    }
    if (auto res = create_staging_texture(); !res) {
        return res;
    }

    return {};
}
VoidResult ScreenRecorderWindows::reinit() {
    staging_texture_.Reset();
    desk_dupl_.Reset();
    d3d_context_.Reset();
    d3d_device_.Reset();

    if (auto res = create_device(); !res) {
        return res;
    }
    if (auto res = recreate_desk_duplication(); !res) {
        return res;
    }
    if (auto res = create_staging_texture(); !res) {
        return res;
    }

    d3d_context_->Unmap(staging_texture_.Get(), 0);

    return {};
}

void ScreenRecorderWindows::capture_frame() {
    // Push black frame since monitor has detected a lock
    if (monitor_->force_black_flag_.load(std::memory_order::acquire)) {
        push_black_frame(get_next_frame_pts());
        return;
    }

    // Reinit since monitor detected an unlock/resume
    if (monitor_->need_video_reinit_.exchange(false, std::memory_order::acq_rel)) {
        if (auto res = reinit_duplication(); !res) {
            Log::video_capture()->warn("Failed reiniting duplication: {}", res.error().what());
            monitor_->need_video_reinit_.store(true, std::memory_order::release);
        }
        else {
            Log::video_capture()->debug("Reinitialized duplication context after lock");
        }

        push_black_frame(get_next_frame_pts());
        return;
    }

    if ((desk_dupl_ == nullptr) || (staging_texture_ == nullptr) || (d3d_context_ == nullptr)) {
        push_black_frame(get_next_frame_pts());
        return;
    }

    // Acquire the next desktop image from the duplication API
    ComPtr<IDXGIResource> desktop_resource;
    DXGI_OUTDUPL_FRAME_INFO frame_info;

    HRESULT hres = desk_dupl_->AcquireNextFrame(0, &frame_info, &desktop_resource);

    // when not getting a desktop image, reuse last frame in order to not mess up
    // frame rate
    if (hres == DXGI_ERROR_WAIT_TIMEOUT) {
        int64_t pts = get_next_frame_pts();

        if (last_frame_ != nullptr) {
            if (auto res = encoder_queue_.push_frame(last_frame_, final_stride_, pts); !res) {
                Log::video_capture()->warn("Failed pushing frame to encoder queue: {}", res.error().what());
            }
            else {
                Log::video_capture()->trace("Pushed to encoder queue");
            }
        }
        else {
            push_black_frame(pts);
        }

        monitor_->force_black_flag_.store(false, std::memory_order::release);
        return;
    }

    if (hres == DXGI_ERROR_ACCESS_LOST) {
        monitor_->need_video_reinit_.store(true, std::memory_order::release);

        push_black_frame(get_next_frame_pts());
        return;
    }

    if (hres == DXGI_ERROR_DEVICE_REMOVED || hres == DXGI_ERROR_DEVICE_RESET) {
        if (auto res = reinit(); !res) {
            Log::video_capture()->warn("Failed reiniting duplication: {}", res.error().what());
        }

        push_black_frame(get_next_frame_pts());
        return;
    }

    if (FAILED(hres)) {
        Log::video_capture()->warn("Failed acquiring frame");
        push_black_frame(get_next_frame_pts());
        return;
    }

    // Convert the generic DXGI resource into a D3D11 texture we can copy
    ComPtr<ID3D11Texture2D> acquired_texture;
    hres = desktop_resource.As(&acquired_texture);
    if (FAILED(hres)) {
        Log::video_capture()->warn("Failed converting generic DXGI resource into a D3D11 texture");
        // Must always release the frame if acquired
        desk_dupl_->ReleaseFrame();
        push_black_frame(get_next_frame_pts());
        return;
    }

    // Verify texture dimensions
    D3D11_TEXTURE2D_DESC acquired_desc;
    acquired_texture->GetDesc(&acquired_desc);

    // Copy GPU texture -> CPU-readable staging texture
    d3d_context_->CopyResource(staging_texture_.Get(), acquired_texture.Get());

    // Map the staging texture to access raw pixels on the CPU
    D3D11_MAPPED_SUBRESOURCE mapped_resource;
    hres = d3d_context_->Map(staging_texture_.Get(), 0, D3D11_MAP_READ, 0, &mapped_resource);

    if (FAILED(hres) || (mapped_resource.pData == nullptr)) {
        Log::video_capture()->warn("Failed mapping staging texture");
        desk_dupl_->ReleaseFrame();
        push_black_frame(get_next_frame_pts());
        return;
    }


    int64_t pts = get_next_frame_pts();
    last_frame_ =
        prepare_frame_data(static_cast<uint8_t*>(mapped_resource.pData), mapped_resource.RowPitch, final_stride_);

    d3d_context_->Unmap(staging_texture_.Get(), 0);
    desk_dupl_->ReleaseFrame();

    monitor_->force_black_flag_.store(false, std::memory_order::release);

    if (auto res = encoder_queue_.push_frame(last_frame_, final_stride_, pts); !res) {
        Log::video_capture()->warn("Failed pushing frame to encoder queue: {}", res.error().what());
    }
    else {
        Log::video_capture()->trace("Pushed to encoder queue");
    }
}

void ScreenRecorderWindows::cleanup_platform() {
    // Release DirectX resources
    staging_texture_.Reset();
    desk_dupl_.Reset();
    d3d_context_.Reset();
    d3d_device_.Reset();

    if (monitor_) {
        monitor_->stop();
        monitor_.reset();
    }

    is_initialized_.store(false, std::memory_order::relaxed);
}

Core::RotationType ScreenRecorderWindows::get_rotation_type(int rotation) {
    switch (rotation) {
    case 0: return Core::RotationType::kUnspecified;
    case 1: return Core::RotationType::kRotationIdentity;
    case 2: return Core::RotationType::kRotationRotate90;
    case 3: return Core::RotationType::kRotationRotate180;
    case 4: return Core::RotationType::kRotationRotate270;
    }

    return Core::RotationType::kUnspecified;
}


void ScreenRecorderWindows::ensure_black_frame() {
    if (!black_frame_.empty()) {
        return;
    }

    const auto bytes = final_stride_ * height_;
    black_frame_.assign(bytes, 0);
}
void ScreenRecorderWindows::push_black_frame(int64_t pts) {
    ensure_black_frame();

    last_frame_ = black_frame_.data();
    if (auto res = encoder_queue_.push_frame(last_frame_, final_stride_, pts); !res) {
        Log::video_capture()->trace("Failed pushing BLACK frame: {}", res.error().what());
    }
    else {
        Log::video_capture()->trace("Pushed BLACK frame");
    }
}


#endif // _WIN32