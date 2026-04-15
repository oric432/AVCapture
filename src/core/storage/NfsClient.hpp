#pragma once

#include <string>
#include "utils/error.hpp"
#include "IFileBackend.hpp"

struct nfs_context;
struct nfsfh;

namespace VSCapture::Nfs {

class Client : public IFileBackend {
public:
    ~Client() override;

    Error::VoidResult initialize(std::string server, std::string export_path) override;

    Error::VoidResult mkdir_p(std::string dir) override;
    Error::Result<FileHandle> open_write_trunc(const std::string& path) override;
    Error::Result<FileHandle> open_read(const std::string& path) override;

    Error::Result<int> write(FileHandle& file, const void* data, size_t size) override;
    Error::Result<int> read(FileHandle& file, void* dst, size_t size) override;
    Error::Result<long long> seek(FileHandle& file, long long off, int whence) override;
    Error::VoidResult close(FileHandle& file) override;
    Error::VoidResult unlink(const std::string& path) override;

    Error::VoidResult rename(const std::string& old_name, const std::string& new_name) override;

private:
    static nfsfh* as_nfsfh(FileHandle& file_handle) { return static_cast<nfsfh*>(file_handle.opaque_); }

    static const nfsfh* as_nfsfh(const FileHandle& file_handle) {
        return static_cast<const nfsfh*>(file_handle.opaque_);
    }

    nfs_context* nfs_ = nullptr;
};
} // namespace VSCapture::Nfs