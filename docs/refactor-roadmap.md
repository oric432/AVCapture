# VSCapture Refactoring Roadmap

This document outlines an incremental strategy to improve readability, maintainability, and extensibility of the VSCapture codebase without over-engineering.

## 1. Resource Management via Smart Pointers (C APIs)
Several core encoding classes manage raw C-style pointers from FFmpeg APIs (`AVCodecContext*`, `AVPacket*`, etc.). Manual memory management leads to risk of memory leaks or double-frees, especially in error paths.

**Action Items:**
- Define type aliases for FFmpeg `std::unique_ptr` with custom deleters. For example: 
  ```cpp
  using AVCodecContextPtr = std::unique_ptr<AVCodecContext, decltype(&avcodec_free_context)>;
  ```
- **Target Classes:**
  - `Encoding::VideoEncoder` (Manage `AVCodecContext*`, `AVFrame*`, `AVPacket*`, `SwsContext*` safely).
  - `Encoding::AudioEncoder` (Manage `AVCodecContext*`, `SwrContext*` safely).

## 2. Migration to RAII & Factory Patterns (Eliminating `init/start/stop`)
The current architecture relies heavily on separate `initialize()`, `start()`, and `stop()` methods, leading to "zombie" states where an object is constructed but not fully usable, or where callers forget to call `stop()`.

**Action Items:**
- **Encapsulate Initialization:** Replace separate methods with a static `create(...)` factory method returning an `Error::Result<std::unique_ptr<T>>` (or `shared_ptr`).
- **RAII Compliance:** Move cleanup logic strictly into object destructors (`~T()`) utilizing RAII principles.
- **Target Classes:**
  - `Core::MediaRecorder` (Can also manage its `save_thread_` cleanly on destruction)
  - `Core::AudioCapturer`
  - `Platform::ScreenRecorderBase` & implementations
  - `Encoding::AudioMixer`
  - `Encoding::VideoEncoderQueue`
  - `Core::RollingSegment`
  - `Nfs::Client`

## 3. Architectural Decoupling (Pipeline Refinement)
Downstream components like `RollingSegment` are overly coupled to specific upstream producers (`AudioCapturer` and `IScreenRecorder`). The polling loops hardcode what types of capturers can exist.

**Action Items:**
- **Abstract Frame Providers:** Create an abstract `IFrameProvider` interface (or a shared thread-safe queue sink) so that `RollingSegment` does not need to know about the concrete capturer classes (`AudioCapturer`, `IScreenRecorder`).
- **Inject Dependencies:** Pass these abstractions into the `RollingSegment` consumer. 
- **Benefit:** Allows easy swapping of capturers and mock implementations for testing, minimizing the blast radius of changes.

## 4. Concurrency & Const-Correctness Enhancements
The codebase already makes efficient use of `std::jthread` and lock-free queues (`ReaderWriterQueue`), providing a solid concurrent foundation. But some interfaces can be hardened.

**Action Items:**
- Ensure getter methods (like `get_encoded_frames()`) use the `[[nodiscard]]` attribute and are marked `const`.
- Evaluate `App` (in `main.cpp`) to verify encapsulation. Limit shared state.

---

### Execution Strategy

1. **Phase 1: Smart Pointers and Safe Resources.** Modify `VideoEncoder` and `AudioEncoder`. These are highly scoped changes that improve stability natively with zero architectural side effects.
2. **Phase 2: Factory Patterns & RAII.** Convert the standalone classes (`NfsClient`, `AudioMixer`, `VideoEncoderQueue`), and verify they compile/run, then tackle complex orchestrators (`MediaRecorder`, `App`, `main.cpp`).
3. **Phase 3: Pipeline Decoupling.** Extract the interfaces connecting the recording components with the `RollingSegment` consumer and rewrite the `RollingSegment` pipeline to depend on abstractions.
4. **Phase 4: Polish.** Pass through the codebase standardizing const-correctness and explicit attributes.
