#pragma once

#include <cerrno>
#include <string>
#include <system_error>

namespace chatlab {

[[noreturn]] inline void raiseErrno(const std::string& context, int code = errno) {
    throw std::system_error(code, std::generic_category(), context);
}

}
