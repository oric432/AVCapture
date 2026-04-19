#pragma once
#include <filesystem>
#include "utils/error.hpp"

namespace VSCapture::VSLogs {

Error::Result<std::string> read_app_version(const std::filesystem::path& json_path);
Error::VoidResult copy_file(const std::filesystem::path& src, const std::filesystem::path& dst);
Error::Result<std::filesystem::path>
build_bundle_folder(const std::filesystem::path& bundles_root, const std::filesystem::path& src_log, bool save_localy, VsType vs_type, RoleType role_type, std::string_view id = "");

} // namespace VSCapture::VSLogs