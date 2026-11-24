#include "common/io_utils.hpp"
#include "common/unique_fd.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <future>
#include <gtest/gtest.h>
#include <stdexcept>
#include <sys/socket.h>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace lab2::common {
namespace {

using namespace std::chrono_literals;

struct Pipe {
    UniqueFd reader;
    UniqueFd writer;

    Pipe() {
        int fds[2];
        if (pipe(fds) == -1) {
            throw std::system_error(errno, std::generic_category(), "pipe");
        }
        reader.reset(fds[0]);
        writer.reset(fds[1]);
        setNonBlocking(reader.get());
        setNonBlocking(writer.get());
        setCloseOnExec(reader.get());
        setCloseOnExec(writer.get());
    }
};

template <typename Operation> void expectSystemError(Operation operation, int expectedError) {
    try {
        operation();
        ADD_FAILURE() << "Expected a system error";
    } catch (const std::system_error& error) {
        EXPECT_EQ(error.code().value(), expectedError);
    }
}

TEST(IoUtilsTest, RejectsBlockingDescriptor) {
    UniqueFd fd(open("/dev/null", O_RDONLY));
    ASSERT_TRUE(fd);
    char byte{};
    EXPECT_THROW(readExact(fd.get(), &byte, 1), std::invalid_argument);
}

TEST(IoUtilsTest, EmptyPipeReadTimesOut) {
    Pipe pipe;
    char byte{};
    const auto start = std::chrono::steady_clock::now();
    expectSystemError([&] { readExact(pipe.reader.get(), &byte, 1, Deadline(50ms)); }, ETIMEDOUT);
    EXPECT_GE(std::chrono::steady_clock::now() - start, 50ms);
}

TEST(IoUtilsTest, PartialReadThenEofIsAnError) {
    Pipe pipe;
    const char input = 'x';
    std::array<char, 2> output{};
    writeExact(pipe.writer.get(), &input, 1);
    pipe.writer.reset();
    expectSystemError([&] { readExact(pipe.reader.get(), output.data(), output.size()); },
                      ECONNRESET);
    EXPECT_EQ(output[0], input);
}

TEST(IoUtilsTest, ClosedReaderProducesErrorInsteadOfSigpipeTermination) {
    Pipe pipe;
    pipe.reader.reset();
    const char byte = 'x';
    expectSystemError([&] { writeExact(pipe.writer.get(), &byte, 1); }, EPIPE);
}

TEST(IoUtilsTest, FullPipeWriteTimesOut) {
    Pipe pipe;
    const int capacity = fcntl(pipe.writer.get(), F_GETPIPE_SZ);
    ASSERT_GT(capacity, 0);
    const std::vector<char> input(static_cast<std::size_t>(capacity), 'x');
    writeExact(pipe.writer.get(), input.data(), input.size());
    const char byte = 'y';
    const auto start = std::chrono::steady_clock::now();
    expectSystemError([&] { writeExact(pipe.writer.get(), &byte, 1, Deadline(50ms)); }, ETIMEDOUT);
    EXPECT_GE(std::chrono::steady_clock::now() - start, 50ms);
}

TEST(IoUtilsTest, TransfersMoreThanPipeCapacity) {
    Pipe pipe;
    const int capacity = fcntl(pipe.writer.get(), F_GETPIPE_SZ);
    ASSERT_GT(capacity, 0);
    std::vector<unsigned char> input(static_cast<std::size_t>(capacity) * 3 + 1);
    for (std::size_t index = 0; index < input.size(); ++index) {
        input[index] = static_cast<unsigned char>(index % 251);
    }
    std::vector<unsigned char> output(input.size());
    auto writer = std::async(std::launch::async,
                             [&] { writeExact(pipe.writer.get(), input.data(), input.size()); });
    readExact(pipe.reader.get(), output.data(), output.size());
    writer.get();
    EXPECT_EQ(output, input);
}

} // namespace
} // namespace lab2::common
