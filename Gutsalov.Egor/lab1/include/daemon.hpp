#pragma once

#include <filesystem>

namespace application {

class Daemon {
public:
  static Daemon &instance();

  Daemon(const Daemon &) = delete;
  Daemon &operator=(const Daemon &) = delete;

  int run(const std::filesystem::path &config_path,
          const std::filesystem::path &pid_path);

private:
  Daemon() = default;
};

} // namespace application
