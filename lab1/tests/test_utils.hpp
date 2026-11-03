#pragma once
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>

namespace testutil {

// Временная папка, удаляется вместе с содержимым.
struct TempDir {
    std::string path;
    TempDir() {
        char tmpl[] = "/tmp/dirclean_test_XXXXXX";
        path = mkdtemp(tmpl);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

inline void writeFile(const std::string& path, const std::string& content) {
    std::ofstream(path) << content;
}

// Ждёт, пока условие станет истинным (опрос каждые 50 мс).
inline bool waitFor(const std::function<bool()>& cond, int timeoutMs = 5000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < end) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return cond();
}

}  // namespace testutil
