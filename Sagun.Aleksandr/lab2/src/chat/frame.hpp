#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace chat {

std::string encodeFrame(std::string_view text);
std::optional<std::string> takeFrame(std::string& buffer);

} // namespace chat
