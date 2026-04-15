#pragma once

#include "core/MediaRecorder.hpp"
#include "core/sync/SyncMasterServer.hpp"

namespace VSCapture {
struct AppContext {
    Sync::SyncMasterServer* server;
    Core::MediaRecorder* recorder;
};
} // namespace VSCapture