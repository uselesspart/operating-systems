#pragma once
#include "Conn.h"
#include "SharedSems.h"
#include <memory>

class ConnSock : public Conn {
public:
    explicit ConnSock(int fd);
    ~ConnSock() override;

    bool Write(const void* buf, size_t count) override;
    bool Read(void* buf, size_t count) override;

    // socketpair создаётся до fork (иначе пара fd не будет видна обоим процессам). preFork кладёт оба fd в sems->sock_fds
    static void preFork(SharedSems* sems);
    static std::unique_ptr<ConnSock> createHost(SharedSems* sems);
    static std::unique_ptr<ConnSock> createClient(SharedSems* sems);

private:
    int fd_;
};