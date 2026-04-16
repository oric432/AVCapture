#pragma once

#include <cstdint>
#include "types.hpp"
#include "boost/json/object.hpp"
#include "boost/json/serialize.hpp"
#include "boost/system/detail/error_code.hpp"

namespace VSCapture::Sync {

// Protocol message type identifiers
constexpr std::string_view kTypePing    = "ping";
constexpr std::string_view kTypePong    = "pong";
constexpr std::string_view kTypeStartAt = "start_at";
constexpr std::string_view kTypeSaveAt  = "save_at";

// Returns the int64 value at key, or nullptr if missing or wrong type.
inline const int64_t* get_int64(const json::object& obj, std::string_view key) {
    const auto* v = obj.if_contains(key);
    return v ? v->if_int64() : nullptr;
}

// Returns the string value at key, or nullptr if missing or wrong type.
inline const json::string* get_string(const json::object& obj, std::string_view key) {
    const auto* v = obj.if_contains(key);
    return v ? v->if_string() : nullptr;
}

// Extracts one line from a read buffer: returns the line with trailing CR/LF
// stripped, and erases the consumed bytes (including the delimiter) from buf.
inline std::string strip_line(std::string& buf, std::size_t bytes) {
    std::string line = buf.substr(0, bytes);
    buf.erase(0, bytes);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    return line;
}

inline std::string to_line(json::object obj) {
    std::string str = json::serialize(obj);
    str.push_back('\n');
    return str;
}

inline bool parse_line(const std::string& line, json::object& out, boost::system::error_code& jec) {
    auto value = json::parse(line, jec);
    if (jec || !value.is_object()) {
        return false;
    }

    out = value.as_object();
    return true;
}

inline json::object ping(int64_t ping_sent_ns) {
    return {{"type", "ping"}, {"ping_sent", ping_sent_ns}};
}

inline json::object pong(int64_t ping_sent_ns, int64_t ping_recv_ns) {
    return {{"type", "pong"}, {"ping_sent", ping_sent_ns}, {"ping_recv", ping_recv_ns}};
}

inline json::object start_at(int64_t master_ns) {
    return {{"type", "start_at"}, {"at", master_ns}};
}

// Instructs the worker to flush its rolling buffer to the NFS backend at the
// scheduled master clock time. output_path is advisory and may be ignored by
// the worker if it manages its own naming.
inline json::object save_at(int64_t master_ns, std::string output_path) {
    return {{"type", "save_at"}, {"at", master_ns}, {"output_path", output_path}};
}

} // namespace VSCapture::Sync