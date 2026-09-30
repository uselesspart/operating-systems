#include "PathUtils.h"

#include <cstdlib>
#include <memory>

#include <sys/stat.h>

namespace diskmon::paths {

namespace {

struct FreeDeleter {
    void operator()(char* ptr) const noexcept { std::free(ptr); }
};

}

std::optional<std::string> canonical(const std::string& path)
{
    const std::unique_ptr<char, FreeDeleter> resolved(realpath(path.c_str(), nullptr));
    if (!resolved) {
        return std::nullopt;
    }
    return std::string(resolved.get());
}

bool isDirectory(const std::string& path)
{
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool isInside(const std::string& path, const std::string& ancestor)
{
    if (ancestor == "/") {
        return path.size() > 1 && path.front() == '/';
    }
    return path.size() > ancestor.size() && path.compare(0, ancestor.size(), ancestor) == 0
           && path[ancestor.size()] == '/';
}

std::string join(const std::string& dir, const std::string& name)
{
    return dir == "/" ? dir + name : dir + '/' + name;
}

std::string parent(const std::string& path)
{
    const auto slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return ".";
    }
    return slash == 0 ? "/" : path.substr(0, slash);
}

}
