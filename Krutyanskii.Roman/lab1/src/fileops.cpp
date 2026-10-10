#include "fileops.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

#include "common.h"
#include "logger.h"

namespace lab1 {
namespace fileops {
namespace {

/// Reads the names stored directly inside 'path', skipping "." and "..".
/// Returns false if the directory could not be opened, so that a caller never
/// mistakes "nothing to remove" for "everything removed".
bool listNames(const std::string& path, std::vector<std::string>& names) {
    names.clear();

    DIR* dir = opendir(path.c_str());
    if (dir == nullptr) {
        log::error("lab1: cannot read directory %s: %s", path.c_str(), std::strerror(errno));
        return false;
    }

    while (const struct dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }
        names.push_back(name);
    }
    closedir(dir);
    return true;
}

/// Removes a single file, symlink or directory tree.
bool removeEntry(const std::string& path) {
    struct stat info {};
    if (lstat(path.c_str(), &info) == -1) {
        if (errno == ENOENT) {
            return true;
        }
        log::error("lab1: cannot stat %s: %s", path.c_str(), std::strerror(errno));
        return false;
    }

    if (S_ISDIR(info.st_mode)) {
        // The names are collected first: removing entries while iterating the
        // same directory stream is not portable.
        std::vector<std::string> names;
        bool ok = listNames(path, names);
        for (const std::string& name : names) {
            ok = removeEntry(joinPath(path, name)) && ok;
        }
        if (rmdir(path.c_str()) == -1) {
            log::error("lab1: cannot remove directory %s: %s", path.c_str(),
                       std::strerror(errno));
            ok = false;
        }
        return ok;
    }

    if (unlink(path.c_str()) == -1) {
        log::error("lab1: cannot remove file %s: %s", path.c_str(), std::strerror(errno));
        return false;
    }
    return true;
}

}  // namespace

bool exists(const std::string& path) {
    struct stat info {};
    return lstat(path.c_str(), &info) == 0;
}

bool isSymlink(const std::string& path) {
    struct stat info {};
    return lstat(path.c_str(), &info) == 0 && S_ISLNK(info.st_mode);
}

bool isDirectory(const std::string& path) {
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool clearDirectory(const std::string& path) {
    if (!isDirectory(path)) {
        log::error("lab1: %s is not a directory", path.c_str());
        return false;
    }

    std::vector<std::string> names;
    if (!listNames(path, names)) {
        return false;
    }

    bool ok = true;
    for (const std::string& name : names) {
        ok = removeEntry(joinPath(path, name)) && ok;
    }
    return ok;
}

}  // namespace fileops
}  // namespace lab1
