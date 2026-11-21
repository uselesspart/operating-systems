#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace chatlab::wire {

using Bytes = std::vector<std::uint8_t>;

class Error : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class Writer {
  public:
    void u8(std::uint8_t value);
    void u32(std::uint32_t value);
    void i32(std::int32_t value);
    void i64(std::int64_t value);
    void text(const std::string& value);

    const Bytes& bytes() const noexcept { return out_; }
    Bytes take() { return std::move(out_); }

  private:
    void raw(std::uint64_t value, int width);

    Bytes out_;
};

class Reader {
  public:
    explicit Reader(const Bytes& in) : in_(in) {}

    std::uint8_t u8();
    std::uint32_t u32();
    std::int32_t i32();
    std::int64_t i64();
    std::string text(std::size_t limit);
    std::size_t count(std::size_t limit);

    bool finished() const noexcept { return at_ == in_.size(); }

  private:
    std::uint64_t raw(int width);
    void need(std::size_t size) const;

    const Bytes& in_;
    std::size_t at_ = 0;
};

}
