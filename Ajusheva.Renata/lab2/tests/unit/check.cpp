#include "check.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>

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

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
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
        const int before = unit::g_failures;
        try {
            test.body();
        } catch (const std::exception& error) {
            unit::reportFailure(__FILE__, __LINE__, std::string("exception: ") + error.what());
        }
        const bool ok = unit::g_failures == before;
        std::cout << (ok ? "  [+] " : "  [x] ") << test.name << std::endl;
        ++(ok ? passed : failed);
    }

    std::cout << "\n  итого: пройдено " << passed << ", провалено " << failed << "\n";
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
