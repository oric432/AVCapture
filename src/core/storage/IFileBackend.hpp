#pragma once

#include <filesystem>
#include <string>
#include "utils/error.hpp"

namespace VSCapture::Nfs {

struct FileHandle {
    void* opaque_;
};

class IFileBackend {
public:
    IFileBackend() = default;
    virtual ~IFileBackend() = default;

    virtual Error::VoidResult initialize(std::string server, std::string export_path) = 0;

    // create full directory path
    virtual Error::VoidResult mkdir_p(std::string dir) = 0;

    // open file for writing in trunc/append mode
    virtual Error::Result<FileHandle> open_write_trunc(const std::string& path) = 0;
    virtual Error::Result<FileHandle> open_read(const std::string& path) = 0;

    virtual Error::Result<int> write(FileHandle& file, const void* data, size_t size) = 0;
    virtual Error::Result<int> read(FileHandle& file, void* dst, size_t size) = 0;
    virtual Error::Result<long long> seek(FileHandle& file, long long off, int whence) = 0;
    virtual Error::VoidResult close(FileHandle& file) = 0;
    virtual Error::VoidResult unlink(const std::string& path) = 0;

    virtual Error::VoidResult rename(const std::string& old_name, const std::string& new_name) = 0;
};

} // namespace VSCapture::Nfs