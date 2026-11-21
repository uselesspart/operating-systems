#pragma once

#include <cstdint>
#include <string>

namespace chatlab {

using Millis = std::int64_t;

Millis wallMs();
std::string clockText(Millis stamp);

}
