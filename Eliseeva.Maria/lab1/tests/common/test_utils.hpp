#pragma once

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace testutil {

namespace fs = std::filesystem;

// Временный каталог, удаляется вместе с содержимым.
class TempDir {
public:
    TempDir() {
        char tpl[] = "/tmp/dirclean_test.XXXXXX";
        if (!mkdtemp(tpl)) throw std::runtime_error("mkdtemp failed");
        path_ = tpl;
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;

    const fs::path &path() const { return path_; }
    fs::path operator/(const std::string &name) const { return path_ / name; }

private:
    fs::path path_;
};

inline void writeFile(const fs::path &p, const std::string &content = "") {
    fs::create_directories(p.parent_path());
    std::ofstream out(p);
    out << content;
}

// Отсортированные имена элементов каталога (включая скрытые).
inline std::vector<std::string> listNames(const fs::path &dir) {
    std::vector<std::string> names;
    for (const auto &e : fs::directory_iterator(dir)) names.push_back(e.path().filename().string());
    std::sort(names.begin(), names.end());
    return names;
}

} 