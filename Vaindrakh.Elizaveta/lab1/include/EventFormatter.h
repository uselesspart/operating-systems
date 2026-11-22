#pragma once

#include <cstdint>
#include <string>

namespace disk_monitor {

std::string eventMaskToText(std::uint32_t mask);
std::string eventKindToText(std::uint32_t mask);

}  // namespace disk_monitor

