#pragma once

#include <sstream>
#include <string>
#include <vector>

namespace unit {

struct TestCase {
    const char* name;
    void (*body)();
};

std::vector<TestCase>& registry();
void reportFailure(const char* file, int line, const std::string& message);

struct Registrar {
    Registrar(const char* name, void (*body)()) { registry().push_back({name, body}); }
};

template <typename T>
std::string show(const T& value) {
    std::ostringstream out;
    out << value;
    return out.str();
}
std::string show(const std::string& value);
std::string show(const std::vector<std::string>& values);
std::string show(const std::vector<int>& values);

bool contains(const std::string& text, const std::string& part);

}

#define UNIT_JOIN_IMPL(a, b) a##b
#define UNIT_JOIN(a, b) UNIT_JOIN_IMPL(a, b)

#define TEST(name)                                                         \
    static void name();                                                    \
    static const unit::Registrar UNIT_JOIN(registrar_, name)(#name, name); \
    static void name()

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            unit::reportFailure(__FILE__, __LINE__, "CHECK(" #condition ")"); \
        }                                                                     \
    } while (false)

#define CHECK_EQ(actual, expected)                                                                         \
    do {                                                                                                   \
        const auto& unitActual = (actual);                                                                 \
        const auto& unitExpected = (expected);                                                             \
        if (!(unitActual == unitExpected)) {                                                               \
            unit::reportFailure(__FILE__, __LINE__,                                                        \
                                std::string(#actual) + "\n          got:      " + unit::show(unitActual) + \
                                    "\n          expected: " + unit::show(unitExpected));                  \
        }                                                                                                  \
    } while (false)

#define CHECK_THROWS(statement, Type)                                                          \
    do {                                                                                       \
        bool unitThrown = false;                                                               \
        try {                                                                                  \
            statement;                                                                         \
        } catch (const Type&) {                                                                \
            unitThrown = true;                                                                 \
        }                                                                                      \
        if (!unitThrown) {                                                                     \
            unit::reportFailure(__FILE__, __LINE__, "no " #Type " from " #statement);          \
        }                                                                                      \
    } while (false)
