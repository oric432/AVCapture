#pragma once

#include <string>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <spawn.h>
#include <sys/wait.h>

extern char **environ;
#endif

namespace AVCapture::Utils {

// Runs argv[0] with the remaining elements as arguments. No shell is
// involved, so arguments are never subject to shell interpretation or
// injection. Returns the process's exit code, or -1 if it could not be
// started or waited on.
inline int run_process(const std::vector<std::string> &argv) {
  if (argv.empty()) {
    return -1;
  }

  std::vector<char *> c_argv;
  c_argv.reserve(argv.size() + 1);
  for (const auto &arg : argv) {
    c_argv.push_back(const_cast<char *>(arg.c_str()));
  }
  c_argv.push_back(nullptr);

#if defined(_WIN32)

  const intptr_t handle = _spawnvp(_P_WAIT, c_argv[0], c_argv.data());
  if (handle == -1) {
    return -1;
  }
  return static_cast<int>(handle);

#else

  pid_t pid = 0;
  if (posix_spawnp(&pid, c_argv[0], nullptr, nullptr, c_argv.data(),
                   environ) != 0) {
    return -1;
  }

  int status = 0;
  if (waitpid(pid, &status, 0) == -1) {
    return -1;
  }

  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  return -1;

#endif
}

} // namespace AVCapture::Utils
