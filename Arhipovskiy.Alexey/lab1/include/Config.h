#ifndef DISKMON_CONFIG_H
#define DISKMON_CONFIG_H

#include <optional>
#include <string>
#include <vector>

namespace diskmon {

// One directory per line, relative paths are resolved against the config file location
struct Config {
    std::vector<std::string> directories;

    [[nodiscard]] static std::optional<Config> load(const std::string& path);
};

}

#endif // DISKMON_CONFIG_H
