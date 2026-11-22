#pragma once

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include <unistd.h>

namespace testutil {

namespace fs = std::filesystem;

// Временный каталог, удаляется при разрушении объекта.
class TempDir {
public:
    TempDir() {
        char tmpl[] = "/tmp/rzaeva_lab1_XXXXXX";
        char* created = ::mkdtemp(tmpl);
        if (created == nullptr) throw std::runtime_error("mkdtemp() failed");
        path_ = created;
    }

    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

inline void writeFile(const fs::path& path, const std::string& content = "") {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

inline std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

inline std::vector<std::string> listNames(const fs::path& dir) {
    std::vector<std::string> names;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; it != end; it.increment(ec))
        names.push_back(it->path().filename().string());
    std::sort(names.begin(), names.end());
    return names;
}

}
