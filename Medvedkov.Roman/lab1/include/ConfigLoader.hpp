#pragma once

#include "Configuration.hpp"
#include <string>

namespace reminder {
// Reads and validates an entire configuration before it is applied.
class ConfigLoader final {
public:
    static Configuration load(const std::string& path);
};
}
