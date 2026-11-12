#include "pid_file.hpp"

#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <syslog.h>
#include <system_error>
#include <thread>

namespace application {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw std::system_error(errno, std::generic_category(), message);
}

pid_t previousPid(int fd) {
  char buffer[64] {};
  const auto count = ::pread(fd, buffer, sizeof(buffer), 0);
  if (count < 0) {
    fail("read PID file");
  }
  std::string text(buffer, static_cast<std::size_t>(count));
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
    text.pop_back();
  }
  long value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc {} || end != text.data() + text.size() || value <= 1 ||
      value > std::numeric_limits<pid_t>::max()) {
    return 0;
  }
  return static_cast<pid_t>(value);
}

std::string executable(const std::filesystem::path &path) {
  std::string name = std::filesystem::read_symlink(path).string();
  // A rebuild may replace the executable while the old daemon is still alive.
  const std::string suffix = " (deleted)";
  if (name.ends_with(suffix)) {
    name.resize(name.size() - suffix.size());
  }
  return name;
}

void terminatePrevious(pid_t pid) {
  const auto process = std::filesystem::path("/proc") / std::to_string(pid);
  if (!std::filesystem::exists(process)) {
    return;
  }
  std::ifstream status(process / "stat");
  std::string stat_line;
  std::getline(status, stat_line);
  const auto name_end = stat_line.rfind(')');
  if (name_end != std::string::npos && name_end + 2 < stat_line.size() &&
      stat_line[name_end + 2] == 'Z') {
    return;
  }
  struct stat info {};
  if (::stat(process.c_str(), &info) < 0) {
    if (errno == ENOENT) {
      return;
    }
    fail("stat old process");
  }
  std::error_code error;
  if (!std::filesystem::exists(process / "exe", error)) {
    if (!std::filesystem::exists(process)) {
      return;
    }
    throw std::runtime_error("Cannot identify process from PID file");
  }
  if (info.st_uid != ::geteuid() ||
      executable(process / "exe") != executable("/proc/self/exe")) {
    throw std::runtime_error("PID file refers to another program; refusing to signal it");
  }
  if (::kill(pid, SIGTERM) < 0 && errno != ESRCH) {
    fail("SIGTERM old daemon");
  }
}

} // namespace

PidFile::PidFile(const std::filesystem::path &path)
    : fd_(::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW |
                                O_NONBLOCK, 0600)) {
  if (fd_.get() < 0) {
    fail("open PID file " + path.string());
  }
  struct stat info {};
  if (::fstat(fd_.get(), &info) < 0) {
    fail("stat PID file");
  }
  if (!S_ISREG(info.st_mode) || info.st_uid != ::geteuid() || info.st_nlink != 1) {
    throw std::runtime_error("PID file must be a regular file owned by the current user");
  }

  // Hold this lock until shutdown. Concurrent starts can never both own it.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  bool signalled = false;
  for (;;) {
    if (::flock(fd_.get(), LOCK_EX | LOCK_NB) == 0) {
      break;
    }
    if (errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR) {
      fail("lock PID file");
    }
    if (!signalled) {
      const auto old_pid = previousPid(fd_.get());
      if (old_pid > 1) {
        terminatePrevious(old_pid);
        signalled = true;
      }
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      throw std::runtime_error("Timed out waiting for previous daemon's PID lock");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  // Also inspect a stale PID file, as required, even when its lock is free.
  if (!signalled) {
    const auto old_pid = previousPid(fd_.get());
    if (old_pid > 1) {
      terminatePrevious(old_pid);
    }
  }
  const auto text = std::to_string(::getpid()) + "\n";
  if (::ftruncate(fd_.get(), 0) < 0 ||
      ::pwrite(fd_.get(), text.data(), text.size(), 0) != static_cast<ssize_t>(text.size()) ||
      ::fsync(fd_.get()) < 0) {
    fail("write PID file");
  }
  owned_ = true;
}

PidFile::~PidFile() {
  if (owned_) {
    // Keep the inode, otherwise an unlink/open race could bypass flock.
    if (::ftruncate(fd_.get(), 0) < 0 || ::fsync(fd_.get()) < 0) {
      ::syslog(LOG_ERR, "Cannot clear PID file: errno=%d", errno);
    }
  }
}

} // namespace application
