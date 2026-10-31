#include "daemon.hpp"
#include "config.hpp"
#include "pid_file.hpp"
#include "unique_fd.hpp"
#include "worker.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syslog.h>
#include <system_error>
#include <vector>

namespace application {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw std::system_error(errno, std::generic_category(), message);
}

void closeInheritedDescriptors(int notification_fd) {
  std::vector<int> descriptors;
  // Finish enumeration before closing the directory iterator's own descriptor.
  for (const auto &entry : std::filesystem::directory_iterator("/proc/self/fd")) {
    const int fd = std::stoi(entry.path().filename().string());
    if (fd != notification_fd) {
      descriptors.push_back(fd);
    }
  }
  for (int fd : descriptors) {
    ::close(fd);
  }
  const int null_fd = ::open("/dev/null", O_RDWR);
  if (null_fd < 0) {
    fail("open /dev/null");
  }
  const UniqueFd saved(::fcntl(null_fd, F_DUPFD_CLOEXEC, 3));
  ::close(null_fd);
  if (saved.get() < 0) {
    fail("duplicate /dev/null");
  }
  for (int fd = 0; fd <= 2; ++fd) {
    if (::dup2(saved.get(), fd) < 0) {
      fail("redirect standard descriptors");
    }
  }
}

void notifyParent(int fd, char result) {
  ssize_t written;
  do {
    written = ::write(fd, &result, 1);
  } while (written < 0 && errno == EINTR);
  ::close(fd);
}

void eventLoop(const std::filesystem::path &config_path, Config config, int signal_fd) {
  using Clock = std::chrono::steady_clock;
  auto next_iteration = Clock::now();
  for (;;) {
    const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(next_iteration - Clock::now());
    const int timeout = static_cast<int>(std::clamp<long long>(remaining.count(), 0, INT_MAX));
    pollfd descriptor {signal_fd, POLLIN, 0};
    if (::poll(&descriptor, 1, timeout) < 0) {
      if (errno == EINTR) {
        continue;
      }
      fail("poll signals");
    }
    if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) {
      throw std::runtime_error("Signal descriptor failed");
    }
    bool reload = false;
    bool stop = false;
    if (descriptor.revents & POLLIN) {
      for (;;) {
        signalfd_siginfo signal {};
        const auto count = ::read(signal_fd, &signal, sizeof(signal));
        if (count < 0 && errno == EINTR) {
          continue;
        }
        if (count < 0 && errno == EAGAIN) {
          break;
        }
        if (count != sizeof(signal)) {
          throw std::runtime_error("Cannot read signal descriptor");
        }
        stop = stop || signal.ssi_signo == SIGTERM;
        reload = reload || signal.ssi_signo == SIGHUP;
      }
    }
    if (stop) {
      ::syslog(LOG_INFO, "SIGTERM received, daemon exiting");
      return;
    }
    if (reload) {
      try {
        const auto updated = Config::load(config_path);
        config = updated;
        next_iteration = Clock::now();
        ::syslog(LOG_INFO, "Configuration reloaded: %s", config_path.c_str());
      } catch (const std::exception &error) {
        ::syslog(LOG_ERR, "Configuration reload failed; keeping previous settings: %s", error.what());
      }
    }
    if (Clock::now() >= next_iteration) {
      try {
        const auto result = Worker(config.source_dir, config.target_dir).makeIteration();
        for (const auto &error : result.errors) {
          ::syslog(LOG_ERR, "%s", error.c_str());
        }
        ::syslog(LOG_INFO, "Iteration completed: %zu file(s)", result.processed);
      } catch (const std::exception &error) {
        ::syslog(LOG_ERR, "Iteration failed: %s", error.what());
      }
      next_iteration = Clock::now() + config.interval;
    }
  }
}

} // namespace

Daemon &Daemon::instance() {
  static Daemon daemon;
  return daemon;
}

int Daemon::run(const std::filesystem::path &config_path,
                const std::filesystem::path &pid_path) {
  // Validate before replacing a running daemon.
  const auto config = Config::load(config_path);
  int notification[2];
  if (::pipe2(notification, O_CLOEXEC) < 0) {
    fail("create startup pipe");
  }
  // Do not let redirection overwrite the pipe if the caller closed stdin/out/err.
  for (int &fd : notification) {
    if (fd <= STDERR_FILENO) {
      const int replacement = ::fcntl(fd, F_DUPFD_CLOEXEC, 3);
      if (replacement < 0) {
        const int saved_errno = errno;
        ::close(notification[0]);
        ::close(notification[1]);
        errno = saved_errno;
        fail("duplicate startup pipe");
      }
      ::close(fd);
      fd = replacement;
    }
  }
  const UniqueFd reader(notification[0]);
  sigset_t signals;
  sigset_t previous_mask;
  ::sigemptyset(&signals);
  ::sigaddset(&signals, SIGHUP);
  ::sigaddset(&signals, SIGTERM);
  // No asynchronous signal handlers are installed. Block before fork so every
  // daemon child inherits this mask; eventLoop consumes signals via signalfd.
  // All C++/syslog/config operations therefore run in normal execution context.
  // If application threads are added, block in the initial thread before
  // creating them as well (each thread has its own signal mask).
  if (::sigprocmask(SIG_BLOCK, &signals, &previous_mask) < 0) {
    ::close(notification[1]);
    fail("block signals");
  }
  const pid_t child = ::fork();
  if (child < 0) {
    const int saved_errno = errno;
    ::sigprocmask(SIG_SETMASK, &previous_mask, nullptr);
    ::close(notification[1]);
    errno = saved_errno;
    fail("fork");
  }
  if (child > 0) {
    ::close(notification[1]);
    ::sigprocmask(SIG_SETMASK, &previous_mask, nullptr);
    char result = 0;
    ssize_t count;
    do {
      count = ::read(reader.get(), &result, 1);
    } while (count < 0 && errno == EINTR);
    while (::waitpid(child, nullptr, 0) < 0 && errno == EINTR) {
    }
    if (count != 1 || result != '1') {
      std::cerr << "Daemon startup failed; see syslog (identifier: gutsalov-lab1)\n";
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  }

  bool ready = false;
  int exit_code = EXIT_FAILURE;
  try {
    if (::setsid() < 0) {
      fail("setsid");
    }
    const pid_t second_child = ::fork();
    if (second_child < 0) {
      fail("second fork");
    }
    if (second_child > 0) {
      ::_exit(EXIT_SUCCESS);
    }
    ::umask(0077);
    if (::chdir("/") < 0) {
      fail("chdir /");
    }
    // Reset syslog's saved socket before its descriptor can be reused.
    ::closelog();
    closeInheritedDescriptors(notification[1]);
    ::openlog("gutsalov-lab1", LOG_PID | LOG_NDELAY, LOG_DAEMON);
    const UniqueFd signal_fd(::signalfd(-1, &signals, SFD_CLOEXEC | SFD_NONBLOCK));
    if (signal_fd.get() < 0) {
      fail("signalfd");
    }
    const PidFile pid_file(pid_path);
    ::syslog(LOG_INFO, "Daemon started; config=%s; pid=%ld", config_path.c_str(), static_cast<long>(::getpid()));
    notifyParent(notification[1], '1');
    ready = true;
    eventLoop(config_path, config, signal_fd.get());
    exit_code = EXIT_SUCCESS;
  } catch (const std::exception &error) {
    ::syslog(LOG_ERR, "Daemon failed: %s", error.what());
  }
  if (!ready) {
    notifyParent(notification[1], '0');
  }
  ::closelog();
  // The final daemon runs sanitizer/atexit hooks. Only the intermediate fork
  // uses _exit, so it cannot flush inherited buffers or run parent's hooks.
  std::exit(exit_code);
}

} // namespace application
