
#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>

namespace check {

inline int& totalCount() {
    static int value = 0;
    return value;
}

inline int& failedCount() {
    static int value = 0;
    return value;
}

inline void record(bool passed, const char* file, int line, const char* text) {
    ++totalCount();
    if (!passed) {
        ++failedCount();
        std::cerr << "FAIL " << file << ':' << line << ": " << text << '\n';
    }
}

template <typename A, typename B>
void recordEqual(const A& actual, const B& expected, const char* file, int line,
                 const char* text) {
    ++totalCount();
    if (!(actual == expected)) {
        ++failedCount();
        std::cerr << "FAIL " << file << ':' << line << ": " << text << "\n  expected: "
                  << expected << "\n  actual:   " << actual << '\n';
    }
}

inline int summary(const char* name) {
    std::cout << name << ": " << totalCount() << " checks, " << failedCount() << " failed\n";
    return failedCount() == 0 ? 0 : 1;
}

}

#define CHECK(cond) check::record((cond), __FILE__, __LINE__, #cond)

#define CHECK_EQ(actual, expected) \
    check::recordEqual((actual), (expected), __FILE__, __LINE__, #actual " == " #expected)

#define REQUIRE(cond)                                              \
    do {                                                           \
        const bool requireOk_ = (cond);                            \
        check::record(requireOk_, __FILE__, __LINE__, #cond);      \
        if (!requireOk_) {                                         \
            return;                                                \
        }                                                          \
    } while (0)

class TempDir {
public:
    TempDir() {
        std::string pattern =
            (std::filesystem::temp_directory_path() / "lab1_test.XXXXXX").string();
        if (mkdtemp(&pattern[0]) == nullptr) {
            std::cerr << "cannot create temporary directory\n";
            std::exit(2);
        }
        path_ = std::filesystem::canonical(pattern);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }

    std::filesystem::path mkdir(const std::string& relative) const {
        const std::filesystem::path p = path_ / relative;
        std::filesystem::create_directories(p);
        return p;
    }

    std::filesystem::path write(const std::string& relative,
                                const std::string& content = "") const {
        const std::filesystem::path p = path_ / relative;
        std::ofstream(p) << content;
        return p;
    }

private:
    std::filesystem::path path_;
};
