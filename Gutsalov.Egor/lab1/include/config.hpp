#pragma once

#include <chrono>
#include <filesystem>

namespace application {

struct Config {
  std::filesystem::path source_dir;
  std::filesystem::path target_dir;
  std::chrono::seconds interval {45};

  static Config load(const std::filesystem::path &filename);
};

} // namespace application
