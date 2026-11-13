#include "config.hpp"

#include <charconv>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>

namespace application {
namespace {

std::string trim(const std::string &value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return {};
  }
  return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}

} // namespace

Config Config::load(const std::filesystem::path &filename) {
  std::ifstream input(filename);
  if (!input) {
    throw std::runtime_error("Cannot open configuration: " + filename.string());
  }
  std::map<std::string, std::string> values;
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    line = trim(line);
    if (line.empty() || line.front() == '#') {
      continue;
    }
    const auto separator = line.find('=');
    if (separator == std::string::npos) {
      throw std::runtime_error("Expected key=value at line " + std::to_string(line_number));
    }
    const auto key = trim(line.substr(0, separator));
    const auto value = trim(line.substr(separator + 1));
    if (key != "source_dir" && key != "target_dir" && key != "interval") {
      throw std::runtime_error("Unknown configuration key: " + key);
    }
    if (value.empty() || !values.emplace(key, value).second) {
      throw std::runtime_error("Empty or duplicate configuration key: " + key);
    }
  }
  if (input.bad()) {
    throw std::runtime_error("Cannot read configuration: " + filename.string());
  }
  for (const auto *key : {"source_dir", "target_dir", "interval"}) {
    if (!values.contains(key)) {
      throw std::runtime_error(std::string("Missing configuration key: ") + key);
    }
  }
  int seconds = 0;
  const auto &interval = values.at("interval");
  const auto [end, error] = std::from_chars(
      interval.data(), interval.data() + interval.size(), seconds);
  if (error != std::errc {} || end != interval.data() + interval.size() ||
      seconds <= 0) {
    throw std::runtime_error("interval must be a positive integer number of seconds");
  }

  const auto resolve = [&](const std::string &value) {
    std::filesystem::path path(value);
    if (path.is_relative()) {
      path = filename.parent_path() / path;
    }
    path = std::filesystem::canonical(path);
    if (!std::filesystem::is_directory(path)) {
      throw std::runtime_error("Not a directory: " + path.string());
    }
    return path;
  };
  Config config {resolve(values.at("source_dir")),
                 resolve(values.at("target_dir")), std::chrono::seconds(seconds)};
  if (config.source_dir == config.target_dir) {
    throw std::runtime_error("source_dir and target_dir must be different directories");
  }
  return config;
}

} // namespace application
