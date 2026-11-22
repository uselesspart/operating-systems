#pragma once

#include "common.hpp"

#include <mqueue.h>
#include <string>

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
    mqd_t incoming_;
    mqd_t outgoing_;
    std::string h2c_name_;
    std::string c2h_name_;
    bool failed_;
    std::string error_;
};
