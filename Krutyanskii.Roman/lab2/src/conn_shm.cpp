#include "conn_shm.h"
#include <cstring>
#include <unistd.h>

ConnShm::ConnShm() {
    fd = shm_open("/lab2_shm", O_CREAT|O_RDWR, 0666);
    if(fd >= 0) {
        ftruncate(fd, sz);
        addr = mmap(nullptr, sz, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    }
}
ConnShm::~ConnShm() {
    if(addr) munmap(addr, sz);
    if(fd >= 0) close(fd);
    shm_unlink("/lab2_shm");
}
bool ConnShm::Read(void* buf, size_t count) {
    if(!addr) return false;
    std::memcpy(buf, addr, std::min(count, sz));
    return true;
}
bool ConnShm::Write(const void* buf, size_t count) {
    if(!addr) return false;
    std::memcpy(addr, buf, std::min(count, sz));
    return true;
}
