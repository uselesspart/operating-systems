#include "check.hpp"

#include "client/LineInput.hpp"

#include <chrono>
#include <string>

#include <unistd.h>

using namespace chatlab;

namespace {

struct PipePair {
    int read = -1;
    int write = -1;

    PipePair() {
        int fds[2];
        if (::pipe(fds) == 0) {
            read = fds[0];
            write = fds[1];
        }
    }
    ~PipePair() {
        closeWrite();
        ::close(read);
    }
    void put(const std::string& text) const {
        if (::write(write, text.data(), text.size()) < 0) {
            unit::reportFailure(__FILE__, __LINE__, "write to pipe failed");
        }
    }
    void closeWrite() {
        if (write >= 0) {
            ::close(write);
            write = -1;
        }
    }
};

constexpr auto kShort = std::chrono::milliseconds(100);

}

TEST(line_input_splits_complete_lines) {
    PipePair pipe;
    LineInput input(pipe.read);
    pipe.put("один\nдва\nтри");
    CHECK_EQ(input.poll(kShort), (std::vector<std::string>{"один", "два"}));
    pipe.put("\n");
    CHECK_EQ(input.poll(kShort), std::vector<std::string>{"три"});
    CHECK(!input.closed());
}

TEST(line_input_timeout_returns_nothing) {
    PipePair pipe;
    LineInput input(pipe.read);
    CHECK(input.poll(kShort).empty());
    CHECK(!input.closed());
}

TEST(line_input_end_of_stream_flushes_tail) {
    PipePair pipe;
    LineInput input(pipe.read);
    pipe.put("хвост");
    CHECK(input.poll(kShort).empty());
    pipe.closeWrite();
    CHECK_EQ(input.poll(kShort), std::vector<std::string>{"хвост"});
    CHECK(input.closed());
    CHECK(input.poll(kShort).empty());
}
