// Запуск модульных тестов:  unit_tests [часть имени теста]
#include "check.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include <unistd.h>

namespace unit {

namespace {

int g_failures = 0;

}  

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

void reportFailure(const char* file, int line, const std::string& message) {
    ++g_failures;
    std::cout << "        > " << file << ":" << line << ": " << message << "\n";
}

std::string show(const std::string& value) {
    return "\"" + value + "\"";
}

std::string show(const std::vector<std::string>& values) {
    std::string result = "{";
    for (std::size_t i = 0; i < values.size(); ++i) {
        result += (i > 0 ? ", " : "") + show(values[i]);
    }
    return result + "}";
}

std::string show(const std::vector<int>& values) {
    std::string result = "{";
    for (std::size_t i = 0; i < values.size(); ++i) {
        result += (i > 0 ? ", " : "") + std::to_string(values[i]);
    }
    return result + "}";
}

std::vector<std::string> lines(std::initializer_list<std::string> items) {
    return std::vector<std::string>(items);
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

TempDir::TempDir() {
    const char* base = std::getenv("TMPDIR");
    std::string pattern =
        std::string(base != nullptr && *base != '\0' ? base : "/tmp") + "/diskmond-unit.XXXXXX";
    if (::mkdtemp(pattern.data()) == nullptr) {
        throw std::runtime_error("mkdtemp failed: " + std::string(std::strerror(errno)));
    }
    path_ = std::filesystem::canonical(pattern).string();
}

TempDir::~TempDir() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
}

void writeFile(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    out << content;
    if (!out) {
        throw std::runtime_error("cannot write " + path);
    }
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream content;
    content << in.rdbuf();
    return content.str();
}

}  

int main(int argc, char* argv[]) {
    auto tests = unit::registry();
    std::sort(tests.begin(), tests.end(), [](const unit::TestCase& a, const unit::TestCase& b) {
        return std::strcmp(a.name, b.name) < 0;
    });
    const std::string filter = argc > 1 ? argv[1] : "";

    int passed = 0;
    int failed = 0;
    for (const unit::TestCase& test : tests) {
        if (!filter.empty() && !unit::contains(test.name, filter)) {
            continue;
        }
        const int failuresBefore = unit::g_failures;
        try {
            test.body();
        } catch (const std::exception& error) {
            unit::reportFailure(__FILE__, __LINE__, std::string("exception: ") + error.what());
        }
        const bool ok = unit::g_failures == failuresBefore;
        std::cout << (ok ? "  [+] " : "  [x] ") << test.name << std::endl;
        ++(ok ? passed : failed);
    }

    std::cout << "\n  итого: пройдено " << passed << ", провалено " << failed << "\n";
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
