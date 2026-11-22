#pragma once

#include <cstddef>
#include <memory>

namespace wolf {

enum class Side { host, child };

class Connection {
public:
    static constexpr std::size_t capacity = 256;

    Connection();
    virtual ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    void open(Side side);
    void read(void* data, std::size_t size);
    void write(const void* data, std::size_t size);

private:
    virtual void openTransport(Side side) = 0;
    virtual void onConnected() {}
    virtual void receive(void* data, std::size_t size) = 0;
    virtual void send(const void* data, std::size_t size) = 0;

    struct Signals;
    std::unique_ptr<Signals> signals_;
    Side side_ = Side::host;
};

std::unique_ptr<Connection> makeConnection();

}
