#include "worker.hpp"
#include "file_manager.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace application {

Worker::Worker(std::filesystem::path first_directory,
               std::filesystem::path second_directory)
    : first_dir_(std::move(first_directory)),
      second_dir_(std::move(second_directory)) {}

IterationResult Worker::makeIteration() const {
  namespace fs = std::filesystem;
  IterationResult result;
  const auto destination = second_dir_ / "total.log";
  std::vector<fs::path> sources;

  for (const auto &entry : fs::recursive_directory_iterator(first_dir_)) {
    if (entry.is_symlink() || !entry.is_regular_file() ||
        !entry.path().filename().string().ends_with(".log")) {
      continue;
    }
    // The output may be inside the input tree or have a hard link there.
    std::error_code error;
    if (fs::equivalent(entry.path(), destination, error)) {
      continue;
    }
    sources.push_back(entry.path());
  }
  std::sort(sources.begin(), sources.end());

  const files::FileManager total_log(destination);
  for (const auto &source : sources) {
    try {
      const auto name = source.lexically_relative(first_dir_).generic_string();
      total_log.appendFile(source, "\n\n" + name + "\n\n");
      if (!fs::remove(source)) {
        throw std::runtime_error("Cannot remove source: " + source.string());
      }
      ++result.processed;
    } catch (const std::exception &error) {
      result.errors.push_back(source.string() + ": " + error.what());
    }
  }
  return result;
}

} // namespace application
