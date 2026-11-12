#pragma once

#include <filesystem>
#include <string_view>

namespace files {

class FileManager {
public:
  explicit FileManager(std::filesystem::path filepath);

  // Appends the complete record and synchronizes the output before returning.
  // On read/write errors, attempts to roll back the incomplete record.
  void appendFile(const std::filesystem::path &source,
                  std::string_view header) const;

private:
  std::filesystem::path filepath_;
};

} // namespace files
