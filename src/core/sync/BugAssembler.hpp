#pragma once
#include <filesystem>
#include <string>
#include "utils/error.hpp"
#include "core/storage/IFileBackend.hpp"

namespace VSCapture::Sync {

struct AssemblerSpec {
    std::string nfs_audio_zip_;
    std::string nfs_video_zip_;

    std::string nfs_out_dir_;
    std::string bug_folder_name_;
};

class BugAssembler {
public:
    explicit BugAssembler(Nfs::IFileBackend& nfs);
    Error::VoidResult assemble(const AssemblerSpec& spec);

private: 
    Error::VoidResult download_nfs_to_local(const std::string& nfs_path, const std::filesystem::path& local_path);
    Error::VoidResult upload_local_to_nfs(const std::filesystem::path& local_path, const std::string& nfs_path);

    Error::VoidResult upload_tree(const std::filesystem::path& local_root, const std::string& nfs_root);

    static Error::VoidResult run_process(const std::filesystem::path& exe, const std::string& args);

    static Error::Result<std::filesystem::path> find_first_ext(const std::filesystem::path& root, std::string_view ext);
    static Error::Result<std::filesystem::path> find_logs_dir(const std::filesystem::path& root);
    static Error::VoidResult copy_tree(const std::filesystem::path& src, const std::filesystem::path& dst);


    Nfs::IFileBackend& nfs_;
};
} // namespace VSCapture::Sync