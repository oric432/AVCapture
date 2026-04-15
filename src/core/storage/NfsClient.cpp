#include "NfsClient.hpp"
#include "utils/utils.hpp"
#include <nfsc/libnfs.h>

#if defined(_WIN32)
    #include "io.h"
    #ifndef S_ISDIR
        #define S_ISDIR(m) (((m) & _S_IFDIR) == _S_IFDIR)
    #endif
#else
    #include <sys/stat.h>
#endif

using namespace VSCapture;

namespace VSCapture::Nfs {

Client::~Client() {
    if (nfs_ != nullptr) {
        nfs_destroy_context(nfs_);
        nfs_ = nullptr;
    }
}
Error::VoidResult Client::initialize(std::string server, std::string export_path) {
    nfs_ = nfs_init_context();
    if (nfs_ == nullptr) {
        return std::unexpected(Error::make_error().with_context("Failed initializing nfs context"));
    }


    static constexpr int kTimeoutMs = 2000;
    static constexpr int kRetrans = 2;

    if (auto res = Utils::tcp_connect_with_timeout(server, 2049, std::chrono::milliseconds(kTimeoutMs)); !res) {
        return std::unexpected(res.error().with_context("Failed connecting to nfs server"));
    }

    nfs_set_timeout(nfs_, kTimeoutMs);
    nfs_set_retrans(nfs_, kRetrans);

    // Mount remote export
    if (nfs_mount(nfs_, server.c_str(), export_path.c_str()) != 0) {
        std::string msg = "nfs_mount failed: ";
        msg += nfs_get_error(nfs_);
        nfs_destroy_context(nfs_);
        return std::unexpected(Error::make_error().with_context(msg));
    }

    return {};
}


Error::VoidResult Client::mkdir_p(std::string dir) {
    if (nfs_ == nullptr) {
        return std::unexpected(Error::make_error().with_context("Nfs context not initialized"));
    }

    if (dir.size() > 1 && dir.back() == '/') {
        dir.pop_back();
    }

    if (dir.empty()) {
        return {};
    }

    nfs_stat_64 stat{};

    // Quick check if exists and is dir
    if (nfs_stat64(nfs_, dir.c_str(), &stat) == 0) {
        if (S_ISDIR(stat.nfs_mode)) {
            return {};
        }
        return std::unexpected(
            Error::make_error().with_context(std::format("Path exists but is not a directory {}", dir)));
    }

    // Build incremental prefixes: "/", "/a", "/a/b", ...
    constexpr int kMAX_DIR_PARTS = 32;
    std::vector<std::string> parts;
    parts.reserve(kMAX_DIR_PARTS);

    if (dir[0] != '/') {
        // For safety: libnfs paths are typically absolute within the mounted export
        return std::unexpected(Error::make_error().with_context(std::format("Expected absolute NFS path: {}", dir)));
    }

    std::string cur;
    cur.reserve(dir.size());
    for (size_t i = 0; i < dir.size(); ++i) {
        cur.push_back(dir[i]);
        if (dir[i] == '/' && i != 0) {
            std::string part = cur;
            if (part.size() > 1 && part.back() == '/') {
                part.pop_back();
            }
            parts.push_back(std::move(part));
        }
    }

    parts.push_back(dir);

    // Create each directory component if missing
    for (auto& part : parts) {
        if (part.empty()) {
            continue;
        }

        if (nfs_stat64(nfs_, part.c_str(), &stat) == 0) {
            if (!S_ISDIR(stat.nfs_mode)) {
                return std::unexpected(Error::make_error().with_context(std::format("Not a directory: {}", part)));
            }

            // dir already exists
            continue;
        }

        // Directory does not exist -> create it
        if (nfs_mkdir(nfs_, part.c_str()) != 0) {
            // `nfs_mkdir` may fail because another client created the dir
            // before us – in that case we simply continue.
            if (nfs_stat64(nfs_, part.c_str(), &stat) == 0 && S_ISDIR(stat.nfs_mode)) {
                continue;
            }
            return std::unexpected(
                Error::make_error().with_context(
                    std::format("nfs_mkdir failed for {} : {}", part, nfs_get_error(nfs_))));
        }
    }

    return {};
}

// Open a file for writing - truncate if already exists
Error::Result<FileHandle> Client::open_write_trunc(const std::string& path) {
    if (nfs_ == nullptr) {
        return std::unexpected(Error::make_error().with_context("Nfs context not initialized"));
    }

    if (auto dir = Utils::parent_dir(path); !dir.empty()) {
        if (auto res = mkdir_p(dir); !res) {
            return std::unexpected(res.error());
        }
    }

    nfsfh* handle = nullptr;

    // nfs_open uses POSIX-like open flags (O_CREAT/O_TRUNC/...)
    int nfs_res = nfs_open(nfs_, path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, &handle);
    if (nfs_res < 0) {
        return std::unexpected(
            Error::make_error().with_context(std::format("Failed opening file handle: {}", nfs_get_error(nfs_))));
    }

    return FileHandle{handle};
}

Error::Result<FileHandle> Client::open_read(const std::string& path) {
    if (nfs_ == nullptr) {
        return std::unexpected(Error::make_error().with_context("Nfs context not initialized"));
    }

    nfsfh* handle = nullptr;
    // nfs_open uses POSIX-like open flags (O_CREAT/O_TRUNC/...)
    int nfs_res = nfs_open(nfs_, path.c_str(), O_RDONLY, &handle);
    if (nfs_res < 0) {
        return std::unexpected(
            Error::make_error().with_context(std::format("Failed opening file handle: {}", nfs_get_error(nfs_))));
    }

    return FileHandle{handle};
}

Error::Result<int> Client::write(FileHandle& file, const void* data, size_t size) {
    if (nfs_ == nullptr) {
        return std::unexpected(Error::make_error().with_context("NFS context not initialized"));
    }

    if (file.opaque_ == nullptr) {
        return std::unexpected(Error::make_error().with_context("Invalid NFS file handle"));
    }

    const int written = nfs_write(nfs_, as_nfsfh(file), data, size);
    if (written < 0) {
        return std::unexpected(
            Error::make_error().with_context(std::format("nfs_write failed: ", nfs_get_error(nfs_))));
    }
    return written;
}

Error::Result<int> Client::read(FileHandle& file, void* dst, size_t size) {
    if (nfs_ == nullptr) {
        return std::unexpected(Error::make_error().with_context("NFS context not initialized"));
    }

    if (file.opaque_ == nullptr) {
        return std::unexpected(Error::make_error().with_context("Invalid NFS file handle"));
    }

    const int read_res = nfs_read(nfs_, as_nfsfh(file), dst, size);
    if (read_res < 0) {
        return std::unexpected(Error::make_error().with_context(std::format("nfs_read failed: ", nfs_get_error(nfs_))));
    }

    return read_res;
}

Error::Result<long long> Client::seek(FileHandle& file, long long off, int whence) {
    if (nfs_ == nullptr) {
        return std::unexpected(Error::make_error().with_context("NFS context not initialized"));
    }

    if (file.opaque_ == nullptr) {
        return std::unexpected(Error::make_error().with_context("Invalid NFS file handle"));
    }

    // Needed for classic MP4 muxing (writes moov and then seeks back to patch offsets).
    const off_t newpos = nfs_lseek(nfs_, as_nfsfh(file), static_cast<off_t>(off), whence, nullptr);
    if (newpos < 0) {
        return std::unexpected(
            Error::make_error().with_context(std::format("nfs_lseek failed: ", nfs_get_error(nfs_))));
    }
    return static_cast<long long>(newpos);
}
Error::VoidResult Client::close(FileHandle& file) {
    auto* handle = as_nfsfh(file);
    if ((nfs_ == nullptr) || (handle == nullptr)) {
        return {};
    }

    if (nfs_close(nfs_, handle) != 0) {
        handle = nullptr;
        return std::unexpected(
            Error::make_error().with_context(std::format("nfs_close failed: ", nfs_get_error(nfs_))));
    }

    handle = nullptr;
    return {};
}

Error::VoidResult Client::unlink(const std::string& path) {
    if (nfs_ == nullptr) {
        return std::unexpected(Error::make_error().with_context("NFS context not initialized"));
    }

    if (nfs_unlink(nfs_, path.c_str()) != 0) {
        return std::unexpected(
            Error::make_error().with_context(std::format("nfs_unlink failed: ", nfs_get_error(nfs_))));
    }

    return {};
}


Error::VoidResult Client::rename(const std::string& old_name, const std::string& new_name) {
    if (nfs_ == nullptr) {
        return std::unexpected(Error::make_error().with_context("NFS context not initialized"));
    }

    // Useful for atomic finalize: write "file.tmp" then rename to "file.mp4".
    if (nfs_rename(nfs_, old_name.c_str(), new_name.c_str()) != 0) {
        return std::unexpected(
            Error::make_error().with_context(std::format("nfs_rename failed: ", nfs_get_error(nfs_))));
    }
    return {};
}
} // namespace VSCapture::Nfs