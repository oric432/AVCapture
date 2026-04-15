#pragma once

#include <cstdint>
#include "types.hpp"
#include "boost/json/object.hpp"
#include "boost/json/serialize.hpp"
#include "boost/system/detail/error_code.hpp"

namespace VSCapture::Sync {
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

inline json::object ping(int64_t t1_local_ns) {
    return {{"type", "ping"}, {"t1", t1_local_ns}};
}

inline json::object pong(int64_t t1_local_ns, int64_t t2r_master_ns) {
    return {{"type", "pong"}, {"t1", t1_local_ns}, {"t2r", t2r_master_ns}};
}

inline json::object start_at(int64_t t0_master_ns) {
    return {{"type", "start_at"}, {"t0", t0_master_ns}};
}

inline json::object stop_at(int64_t t_master_ns) {
    return {{"type", "stop_at"}, {"t", t_master_ns}};
}

inline json::object export_at(int64_t t_master_ns, std::string output_path) {
    return {{"type", "export_at"}, {"t", t_master_ns}, {"output_path", output_path}};
}

} // namespace VSCapture::Sync