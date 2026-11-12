#pragma once

#include <cstddef>
#include <memory>

#include "ipc/ConnId.h"

namespace wolf {

// Двусторонний канал между волком и одним козлёнком.
//
// Родственная схема: prepare() вызывается хостом до fork и создаёт ресурсы
// (pipe/socketpair/FIFO). После fork хост строит Conn(id, true), потомок —
// Conn(id, false). Каждый conn_<тип>.cpp реализует этот класс по-своему.
class Conn {
public:
    static void prepare(const ConnId& id);
    static void discard(const ConnId& id);

    Conn(const ConnId& id, bool create);
    ~Conn();

    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;

    bool Read(void* buf, std::size_t count);
    bool Write(const void* buf, std::size_t count);

    static const char* typeCode();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
