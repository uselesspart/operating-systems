#pragma once

#include <cstddef>
#include <string>

namespace chatlab::textclean {

std::string cleanText(const std::string& raw);
std::string cleanName(const std::string& raw);
std::string cutUtf8(const std::string& text, std::size_t maxBytes);

}
