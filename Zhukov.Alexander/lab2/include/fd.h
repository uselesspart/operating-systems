#pragma once

#include <unistd.h>
#include <utility>

class Fd {
public:
    explicit Fd(int value = -1) : value(value) {}
    ~Fd() { reset(); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& other) noexcept : value(std::exchange(other.value, -1)) {}
    Fd& operator=(Fd&& other) noexcept {
        if (this != &other) {
            reset(std::exchange(other.value, -1));
        }
        return *this;
    }
    int get() const { return value; }
    void reset(int next = -1) {
        if (value >= 0) {
            close(value);
        }
        value = next;
    }

private:
    int value;
};
