#pragma once

#include <cassert>
#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <limits.h>
#include <unistd.h>

#endif

namespace AVCapture::Utils {

inline std::filesystem::path get_exe_dir() {
#if defined(_WIN32)

  wchar_t buf[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (n == 0 || n == MAX_PATH) {
    return std::filesystem::current_path();
  }
  return std::filesystem::path(buf).parent_path();

#elif defined(__linux__)

  char buf[PATH_MAX];
  ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n <= 0) {
    return std::filesystem::current_path();
  }
  buf[n] = '\0';
  return std::filesystem::path(buf).parent_path();

#else

  return std::filesystem::current_path();

#endif
}

} // namespace AVCapture::Utils