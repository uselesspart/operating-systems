#include "PathUtil.hpp"

#include <climits>
#include <cstdlib>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

namespace diskmond::path {

namespace {

bool hasType(const std::string& path, mode_t type) {
    struct stat info {};
    return ::stat(path.c_str(), &info) == 0 && (info.st_mode & S_IFMT) == type;
}

}  

std::string join(const std::string& dir, const std::string& name) {
    if (dir.empty()) {
        return name;
    }
    if (name.empty()) {
        return dir;
    }
    return dir.back() == '/' ? dir + name : dir + '/' + name;
}

std::string parentOf(const std::string& path) {
    const auto slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return ".";
    }
    return slash == 0 ? std::string("/") : path.substr(0, slash);
}

std::string nameOf(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool isWithin(const std::string& path, const std::string& base) {
    if (path == base) {
        return true;
    }
    if (base == "/") {
        return !path.empty() && path.front() == '/';
    }
    
    return path.size() > base.size() && path.compare(0, base.size(), base) == 0 && path[base.size()] == '/';
}

std::string normalize(const std::string& path) {
    const bool isAbsolute = !path.empty() && path.front() == '/';
    std::vector<std::string> parts;

    std::size_t begin = 0;
    while (begin <= path.size()) {
        std::size_t end = path.find('/', begin);
        if (end == std::string::npos) {
            end = path.size();
        }
        const std::string part = path.substr(begin, end - begin);
        if (part == "..") {
            if (!parts.empty() && parts.back() != "..") {
                parts.pop_back();
            } else if (!isAbsolute) {
                parts.push_back(part);  
            }
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        begin = end + 1;
    }

    std::string result = isAbsolute ? "/" : "";
    for (std::size_t i = 0; i < parts.size(); ++i) {
        result += (i > 0 ? "/" : "") + parts[i];
    }
    return result.empty() ? "." : result;
}

std::string absolute(const std::string& path) {
    if (!path.empty() && path.front() == '/') {
        return normalize(path);
    }
    char cwd[PATH_MAX];
    const std::string base = ::getcwd(cwd, sizeof cwd) != nullptr ? std::string(cwd) : std::string("/");
    return normalize(join(base, path));
}

std::optional<std::string> canonical(const std::string& path) {
    char resolved[PATH_MAX];
    if (::realpath(path.c_str(), resolved) == nullptr) {
        return std::nullopt;
    }
    return std::string(resolved);
}

std::string resolve(const std::string& path) {
    
    std::string existing = absolute(path);
    std::string missingTail;
    for (;;) {
        if (const auto real = canonical(existing)) {
            return missingTail.empty() ? *real : join(*real, missingTail);
        }
        if (existing == "/") {
            return absolute(path);
        }
        missingTail = missingTail.empty() ? nameOf(existing) : join(nameOf(existing), missingTail);
        existing = parentOf(existing);
    }
}

bool isDirectory(const std::string& path) {
    return hasType(path, S_IFDIR);
}

bool isRegularFile(const std::string& path) {
    return hasType(path, S_IFREG);
}

}  
