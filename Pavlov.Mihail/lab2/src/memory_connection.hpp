#pragma once

#include "connection.hpp"

namespace wolf {

class Mapping {
public:
    Mapping() = default;
    ~Mapping();

    Mapping(const Mapping&) = delete;
    Mapping& operator=(const Mapping&) = delete;

    void map(std::size_t size, int fd = -1);
    void* data() const { return data_; }

private:
    void* data_ = nullptr;
    std::size_t size_ = 0;
};

class MemoryConnection : public Connection {
protected:
    void mapBuffers(int fd = -1);

private:
    void openTransport(Side side) final;
    void receive(void* data, std::size_t size) final;
    void send(const void* data, std::size_t size) final;
    virtual void openMemory(Side side) = 0;

    Mapping buffers_;
    char* incoming_ = nullptr;
    char* outgoing_ = nullptr;
};

}
