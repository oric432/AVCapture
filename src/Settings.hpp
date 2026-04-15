#pragma once

// toml++ config
#define TOML_EXCEPTIONS 0
#include <toml++/toml.hpp>

#include "utils/assert.hpp"
#include "utils/error.hpp"
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace VSCapture {

class Settings {
public:
    // Schema and its defaults
    static constexpr std::string_view kDefaultsToml = R"toml(
        [api]
        address = "127.0.0.1"
        port = 8084

        [recording]
        fps = 30
        bitrate = 4000000
        buffer_duration = 10
        segment_seconds = 2

        [audio]
        output_device_name = ""
        input_device_name  = ""

        [log]
        level = "info"
        max_log_size_bytes = 5242880
        max_files = 5

        [nfs]
        save_locally = false
        server_address = ""
        export_path = ""

        [vs]
        app_path = "C:/KoUpdater"
        type = "VS"

        [sync]
        role = "none"
        bind_address = "0.0.0.0"
        peer_address = "127.0.0.1"
        port = 45000

        )toml";

    struct Path {
        // api
        static constexpr auto kAPI_ADDRESS = "api.address";
        static constexpr auto kAPI_PORT = "api.port";

        // recording
        static constexpr auto kFPS = "recording.fps";
        static constexpr auto kBITRATE = "recording.bitrate";
        static constexpr auto kBUFFER_DURATION = "recording.buffer_duration";
        static constexpr auto kSEGMENT_SECONDS = "recording.segment_seconds";

        // audio
        static constexpr auto kOUTPUT_DEVICE_NAME = "audio.output_device_name";
        static constexpr auto kINPUT_DEVICE_NAME = "audio.input_device_name";

        // log
        static constexpr auto kLOG_LEVEL = "log.level";
        static constexpr auto kMAX_LOG_SIZE_BYTES = "log.max_log_size_bytes";
        static constexpr auto kMAX_FILES = "log.max_files";

        // nfs
        static constexpr auto kSAVE_LOCALLY = "nfs.save_locally";
        static constexpr auto kNFS_SERVER_ADDRESS = "nfs.server_address";
        static constexpr auto kNFS_EXPORT_PATH = "nfs.export_path";

        // vs
        static constexpr auto kVS_APP_PATH = "vs.app_path";
        static constexpr auto kVS_TYPE = "vs.type";


        static constexpr auto kROLE = "sync.role";
        static constexpr auto kSYNC_BIND_ADDRESS = "sync.bind_address";
        static constexpr auto kSYNC_PEER_ADDRESS = "sync.peer_address";
        static constexpr auto kSYNC_PORT = "sync.port";
    };

    static Error::Result<Settings> load(std::string_view file_path);

    template <typename T>
    Error::Result<T> try_get(std::string_view path) const {
        const toml::node_view<const toml::node> node = table_.at_path(path);
        auto value = node.value<T>();
        if (!value) {
            std::ostringstream oss;
            oss << "Missing or wrong type at '" << path << "'";
            return std::unexpected(Error::make_error().with_context(oss.str()));
        }
        return *value;
    }

    // Generic getter by TOML path (nested supported via at_path)
    template <typename T>
    T get(std::string_view path) const {
        auto res = try_get<T>(path);
        ASSERTM(res.has_value(), res ? "" : res.error().what());
        return *res;
    }

    std::string dump() const;

private:
    explicit Settings(const toml::table& table)
        : table_(std::move(table)) {}

    static void merge_into(toml::table& dst, const toml::table& src);

    toml::table table_;
};

} // namespace VSCapture
