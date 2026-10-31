#pragma once



#include <initializer_list>
#include <optional>
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
template <typename T>
std::string show(const std::optional<T>& value) {
    return value ? show(*value) : std::string("nullopt");
}

std::vector<std::string> lines(std::initializer_list<std::string> items);
bool contains(const std::string& text, const std::string& part);


class TempDir {
  public:
    TempDir();
    ~TempDir();
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::string& path() const { return path_; }
    std::string operator/(const std::string& name) const { return path_ + "/" + name; }

  private:
    std::string path_;
};

void writeFile(const std::string& path, const std::string& content);
std::string readFile(const std::string& path);

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
