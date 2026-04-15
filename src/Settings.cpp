#include "Settings.hpp"

namespace VSCapture {

void Settings::merge_into(toml::table& dst, const toml::table& src) {
    // Recursive merge:
    for (const auto& [k, v] : src) {
        if (const auto* src_tbl = v.as_table()) {
            if (toml::node* existing = dst.get(k); (existing != nullptr) && existing->is_table()) {
                merge_into(*existing->as_table(), *src_tbl);
            }
            else {
                dst.insert_or_assign(k, toml::table{*src_tbl});
            }
        }
        else {
            dst.insert_or_assign(k, v);
        }
    }
}

Error::Result<Settings> Settings::load(std::string_view file_path) {
    // 1) Parse defaults (schema baseline)
    auto def_res = toml::parse(std::string(kDefaultsToml));
    if (!def_res) {
        std::string msg = "Internal defaults TOML parse failed: ";
        msg += def_res.error().description();
        return std::unexpected(Error::make_error().with_context(msg));
    }

    toml::table cfg = def_res.table();

    // 2) Parse user file
    auto file_res = toml::parse_file(std::string(file_path));
    if (!file_res) {
        std::string msg = "Settings file parse failed: ";
        msg += file_res.error().description();
        return std::unexpected(Error::make_error().with_context(msg));
    }

    // 3) Merge file onto defaults
    merge_into(cfg, file_res.table());

    return Settings{cfg};
}

std::string Settings::dump() const {
    std::ostringstream oss;
    oss << table_;
    return oss.str();
}
} // namespace VSCapture
