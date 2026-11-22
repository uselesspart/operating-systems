#pragma once

#include <cstddef>
#include <string>

constexpr std::size_t frameSize = 1024;

class Conn {
    int in_ = -1;
    int out_ = -1;
    bool host_;
    std::string name_;

public:
    Conn(bool host, const std::string& name, int hostSocket, int clientSocket);
    ~Conn();
    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;
    void Read(void* buf, std::size_t count);
    void Write(const void* buf, std::size_t count);
};
