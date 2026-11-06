#pragma once

#include "common.hpp"

#include <string>
#include <vector>

class Conn {
public:
    Conn(Role role, pid_t host_pid, SharedSync* sync);
    bool Read(void* buf, std::size_t count);
    bool Write(const void* buf, std::size_t count);
    bool Failed() const;
    std::string Error() const;
    ~Conn();

private:
    void SetError(const std::string& message);

    Role role_;
    pid_t host_pid_;
    SharedSync* sync_;
    int incoming_fd_;
    int outgoing_fd_;
    std::string h2c_path_;
    std::string c2h_path_;
    std::vector<char> read_buffer_;
    bool failed_;
    std::string error_;
};
