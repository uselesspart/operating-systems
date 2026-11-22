#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace application {

struct IterationResult {
  std::size_t processed = 0;
  std::vector<std::string> errors;
};

class Worker {
public:
  Worker(std::filesystem::path first_directory,
         std::filesystem::path second_directory);

  IterationResult makeIteration() const;

private:
  std::filesystem::path first_dir_;
  std::filesystem::path second_dir_;
};

} // namespace application
