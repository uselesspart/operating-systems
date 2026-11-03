#ifndef CHAT_SYSTEM_ERROR_H
#define CHAT_SYSTEM_ERROR_H

#include <cerrno>
#include <string>
#include <system_error>

namespace chat {

[[noreturn]] inline void throwSystemError(const std::string& what, int err = errno)
{
    throw std::system_error(err, std::generic_category(), what);
}

}

#endif // CHAT_SYSTEM_ERROR_H
