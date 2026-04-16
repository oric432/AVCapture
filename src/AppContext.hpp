#pragma once

#include "core/MediaRecorder.hpp"
#include "core/sync/SyncMasterServer.hpp"
#include <variant>

namespace VSCapture {
// Exactly one of: a standalone/worker MediaRecorder, or a sync master server.
using AppContext = std::variant<Core::MediaRecorder*, Sync::SyncMasterServer*>;
} // namespace VSCapture