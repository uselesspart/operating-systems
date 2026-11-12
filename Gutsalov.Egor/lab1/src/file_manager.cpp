#include "file_manager.hpp"
#include "unique_fd.hpp"

#include <array>
#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <sys/file.h>
#include <sys/stat.h>
#include <system_error>
#include <utility>

namespace files {
namespace {

void writeAll(int fd, const char *data, std::size_t size) {
  while (size > 0) {
    const ssize_t written = ::write(fd, data, size);
    if (written < 0 && errno == EINTR) {
      continue;
    }
    if (written <= 0) {
      throw std::system_error(written < 0 ? errno : EIO,
                              std::generic_category(), "write total.log");
    }
    data += written;
    size -= static_cast<std::size_t>(written);
  }
}

struct stat regularFile(int fd, const std::filesystem::path &path) {
  struct stat info {};
  if (fd < 0 || ::fstat(fd, &info) < 0) {
    throw std::system_error(errno, std::generic_category(), path.string());
  }
  if (!S_ISREG(info.st_mode)) {
    throw std::runtime_error("Not a regular file: " + path.string());
  }
  return info;
}

} // namespace

FileManager::FileManager(std::filesystem::path filepath)
    : filepath_(std::move(filepath)) {}

void FileManager::appendFile(const std::filesystem::path &source,
                             std::string_view header) const {
  application::UniqueFd input(
      ::open(source.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
  const auto source_info = regularFile(input.get(), source);
  application::UniqueFd output(::open(filepath_.c_str(), O_WRONLY | O_CREAT |
      O_APPEND | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600));
  regularFile(output.get(), filepath_);
  if (::flock(output.get(), LOCK_EX) < 0) {
    throw std::system_error(errno, std::generic_category(), "lock total.log");
  }
  const auto output_info = regularFile(output.get(), filepath_);
  if (source_info.st_dev == output_info.st_dev &&
      source_info.st_ino == output_info.st_ino) {
    throw std::runtime_error("Source is total.log itself: " + source.string());
  }

  try {
    writeAll(output.get(), header.data(), header.size());
    std::array<char, 64 * 1024> buffer {};
    for (;;) {
      const ssize_t count = ::read(input.get(), buffer.data(), buffer.size());
      if (count < 0 && errno == EINTR) {
        continue;
      }
      if (count < 0) {
        throw std::system_error(errno, std::generic_category(), source.string());
      }
      if (count == 0) {
        break;
      }
      writeAll(output.get(), buffer.data(), static_cast<std::size_t>(count));
    }
    struct stat current {};
    if (::lstat(source.c_str(), &current) < 0 ||
        current.st_dev != source_info.st_dev ||
        current.st_ino != source_info.st_ino ||
        current.st_size != source_info.st_size ||
        current.st_mtim.tv_sec != source_info.st_mtim.tv_sec ||
        current.st_mtim.tv_nsec != source_info.st_mtim.tv_nsec) {
      throw std::runtime_error("Source changed during copying; kept: " + source.string());
    }
    if (::fsync(output.get()) < 0) {
      throw std::system_error(errno, std::generic_category(), "fsync total.log");
    }
  } catch (...) {
    if (::ftruncate(output.get(), output_info.st_size) == 0) {
      ::fsync(output.get());
    }
    throw;
  }
}

} // namespace files
