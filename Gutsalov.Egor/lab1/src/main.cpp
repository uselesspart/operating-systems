#include "config.hpp"
#include "daemon.hpp"
#include "worker.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <syslog.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  ::openlog("gutsalov-lab1", LOG_PID | LOG_NDELAY, LOG_DAEMON);
  int result = EXIT_FAILURE;
  try {
    std::filesystem::path config_path = "lab1.conf";
    std::filesystem::path pid_path = std::filesystem::path("/tmp") /
        ("gutsalov-lab1-" + std::to_string(::geteuid()) + ".pid");
    bool once = false;
    bool has_config = false;
    for (int i = 1; i < argc; ++i) {
      const std::string argument(argv[i]);
      if (argument == "--help") {
        std::cout << "Usage: lab1 [CONFIG] [--pid-file PATH] [--once]\n"
                     "Default config: ./lab1.conf; --once processes logs without daemonizing.\n";
        ::closelog();
        return EXIT_SUCCESS;
      }
      if (argument == "--once") {
        once = true;
      } else if (argument == "--pid-file") {
        if (++i >= argc) {
          throw std::runtime_error("--pid-file requires a path");
        }
        pid_path = argv[i];
      } else if (argument.starts_with('-') || has_config) {
        throw std::runtime_error("Unexpected argument: " + argument);
      } else {
        config_path = argument;
        has_config = true;
      }
    }
    // Keep the pathname (not the inode) so atomic config replacement also works.
    config_path = std::filesystem::absolute(config_path).lexically_normal();
    pid_path = std::filesystem::absolute(pid_path).lexically_normal();
    if (once) {
      const auto config = application::Config::load(config_path);
      const auto iteration = application::Worker(config.source_dir, config.target_dir).makeIteration();
      for (const auto &error : iteration.errors) {
        std::cerr << error << '\n';
        ::syslog(LOG_ERR, "%s", error.c_str());
      }
      result = iteration.errors.empty() ? EXIT_SUCCESS : EXIT_FAILURE;
    } else {
      result = application::Daemon::instance().run(config_path, pid_path);
    }
  } catch (const std::exception &error) {
    std::cerr << "lab1: " << error.what() << '\n';
    ::syslog(LOG_ERR, "%s", error.what());
  }
  ::closelog();
  return result;
}
