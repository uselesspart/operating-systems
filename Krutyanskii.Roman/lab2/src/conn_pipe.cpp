#include "conn_pipe.h"
#include <unistd.h>
#include <poll.h>

ConnPipe::ConnPipe() { pipe(pipefd); }
ConnPipe::~ConnPipe() { if(pipefd[0]>=0) close(pipefd[0]); if(pipefd[1]>=0) close(pipefd[1]); }
bool ConnPipe::Read(void* buf, size_t count) {
    size_t total = 0;
    char* b = (char*)buf;
    while(total < count) {
        struct pollfd pfd = {pipefd[0], POLLIN, 0};
        if(poll(&pfd, 1, 5000) <= 0) return false;
        ssize_t n = read(pipefd[0], b+total, count-total);
        if(n <= 0) return false;
        total += n;
    }
    return true;
}
bool ConnPipe::Write(const void* buf, size_t count) {
    ssize_t n = write(pipefd[1], buf, count);
    return n == (ssize_t)count;
}
