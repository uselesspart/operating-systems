#include "conn_mmap.h"
#include <cstring>
#include <unistd.h>

ConnMmap::ConnMmap() {
    addr = mmap(nullptr, sz, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_SHARED, -1, 0);
}
ConnMmap::~ConnMmap() { if(addr) munmap(addr, sz); }
bool ConnMmap::Read(void* buf, size_t count) {
    std::memcpy(buf, addr, std::min(count, sz));
    return true;
}
bool ConnMmap::Write(const void* buf, size_t count) {
    std::memcpy(addr, buf, std::min(count, sz));
    return true;
}
