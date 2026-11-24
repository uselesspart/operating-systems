#include "ipc/synchronized_stream.hpp"

#include "common/deadline.hpp"
#include "common/io_utils.hpp"

#include <algorithm>
#include <cerrno>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace lab2::ipc {
namespace {

// Блок помещается даже в минимальный POSIX PIPE_BUF. Если сначала записать
// большое сообщение целиком, читатель может ждать семафор, а писатель — место в канале.
constexpr std::size_t kTransferChunkSize = 512;

void requireTime(const common::Deadline& deadline) {
    if (deadline.remainingMilliseconds() == 0) {
        throw std::system_error(ETIMEDOUT, std::generic_category(), "Transfer deadline");
    }
}

} // namespace

SynchronizedStream::SynchronizedStream(common::UniqueFd reader, common::UniqueFd writer,
                                       common::NamedSemaphore incoming,
                                       common::NamedSemaphore outgoing,
                                       std::chrono::milliseconds timeout)
    : reader_(std::move(reader)), writer_(std::move(writer)), incoming_(std::move(incoming)),
      outgoing_(std::move(outgoing)), timeout_(timeout) {
    if (!reader_) {
        throw std::invalid_argument("Missing read descriptor");
    }
    static_cast<void>(common::Deadline(timeout_));
}

void SynchronizedStream::attachWriter(common::UniqueFd writer) {
    if (!reader_ || writer_ || !writer) {
        throw std::logic_error("Cannot attach write descriptor");
    }
    writer_ = std::move(writer);
}

bool SynchronizedStream::isConnected() const noexcept {
    return static_cast<bool>(reader_) && static_cast<bool>(writer_);
}

void SynchronizedStream::requireConnected() const {
    if (!isConnected()) {
        throw std::logic_error("Connection is not open");
    }
}

void SynchronizedStream::close() noexcept {
    reader_.reset();
    writer_.reset();
}

void SynchronizedStream::read(void* buffer, std::size_t count) {
    requireConnected();
    if (count == 0) {
        return;
    }
    if (buffer == nullptr) {
        throw std::invalid_argument("Null read buffer");
    }

    try {
        const common::Deadline deadline(timeout_);
        auto* bytes = static_cast<unsigned char*>(buffer);
        for (std::size_t offset = 0; offset < count;) {
            if (offset != 0) {
                requireTime(deadline);
            }
            const auto chunk = std::min(kTransferChunkSize, count - offset);
            if (!incoming_.wait(deadline)) {
                throw std::system_error(ETIMEDOUT, std::generic_category(), "Read semaphore");
            }
            common::readExact(reader_.get(), bytes + offset, chunk, deadline);
            offset += chunk;
        }
    } catch (...) {
        // После частичной передачи нельзя восстановить границы следующего сообщения.
        close();
        throw;
    }
}

void SynchronizedStream::write(const void* buffer, std::size_t count) {
    requireConnected();
    if (count == 0) {
        return;
    }
    if (buffer == nullptr) {
        throw std::invalid_argument("Null write buffer");
    }

    try {
        const common::Deadline deadline(timeout_);
        const auto* bytes = static_cast<const unsigned char*>(buffer);
        for (std::size_t offset = 0; offset < count;) {
            if (offset != 0) {
                requireTime(deadline);
            }
            const auto chunk = std::min(kTransferChunkSize, count - offset);
            common::writeExact(writer_.get(), bytes + offset, chunk, deadline);
            outgoing_.post();
            offset += chunk;
        }
    } catch (...) {
        close();
        throw;
    }
}

} // namespace lab2::ipc
