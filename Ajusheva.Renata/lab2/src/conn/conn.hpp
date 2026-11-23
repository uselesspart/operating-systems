#pragma once

#include "link/ConnKey.hpp"

#include <cstddef>
#include <memory>

namespace chatlab {

class Conn {
  public:
    Conn(const ConnKey& key, bool create);
    ~Conn();

    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;

    bool Read(void* buf, std::size_t count);
    bool Write(const void* buf, std::size_t count);

    static const char* Kind();

  private:
    struct State;
    std::unique_ptr<State> state_;
};

}
