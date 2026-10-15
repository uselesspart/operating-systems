#include "config.h"

#include <algorithm>
#include <cerrno>
#include <stdexcept>
#include <string>
#include <system_error>
#include <sys/inotify.h>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

namespace {
    void check_directory(const std::filesystem::path& path) {
        if (!std::filesystem::is_directory(path)) {
            throw std::runtime_error("Not a directory: " + path.string());
        }

        if (access(path.c_str(), R_OK | X_OK) != 0) {
            throw std::system_error(errno, std::generic_category(),
                                    "Cannot access " + path.string());
        }

        const int fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
        if (fd < 0) {
            throw std::system_error(errno, std::generic_category(), "Cannot open inotify");
        }

        const int wd = inotify_add_watch(fd, path.c_str(), IN_ALL_EVENTS | IN_ONLYDIR);
        const int error = errno;
        close(fd);

        if (wd < 0) {
            throw std::system_error(error, std::generic_category(),
                                    "Cannot watch " + path.string());
        }
    }
}

Config load_config(const std::filesystem::path& config_path) {
    const auto file_path = std::filesystem::absolute(config_path);
    std::error_code file_error;
    const auto status = std::filesystem::status(file_path, file_error);
    if (file_error) {
        throw std::runtime_error("Cannot read config: " + file_path.string());
    }
    if (!std::filesystem::is_regular_file(status)) {
        throw std::runtime_error("Not a config file: " + file_path.string());
    }
    YAML::Node root;

    try {
        root = YAML::LoadFile(file_path.string());
    } catch (const YAML::BadFile&) {
        throw std::runtime_error("Cannot read config: " + file_path.string());
    } catch (const YAML::Exception&) {
        throw std::runtime_error("Invalid YAML: " + file_path.string());
    }

    if (!root.IsMap()) {
        throw std::runtime_error("Invalid config");
    }

    const auto paths = root["paths"];
    if (!paths.IsSequence() || paths.size() == 0) {
        throw std::runtime_error("Invalid paths list");
    }

    Config config;
    for (const auto& entry : paths) {
        if (!entry.IsScalar()) {
            throw std::runtime_error("Invalid path");
        }

        const auto name = entry.as<std::string>();
        if (name.empty() || name.find('\0') != std::string::npos) {
            throw std::runtime_error("Invalid path");
        }

        std::filesystem::path path(name);
        if (path.is_relative()) {
            path = file_path.parent_path() / path;
        }
        std::error_code error;
        const auto directory = std::filesystem::canonical(path, error);
        if (error) {
            throw std::system_error(error, "Cannot open directory " + path.string());
        }
        path = directory;

        if (std::find(config.paths.begin(), config.paths.end(), path) == config.paths.end()) {
            check_directory(path);
            config.paths.push_back(path);
        }
    }

    return config;
}
