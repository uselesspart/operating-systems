#include "common.h"

#if defined(TYPE_MMAP)
static const char* kType = "mmap";
#elif defined(TYPE_SHM)
static const char* kType = "shm";
#elif defined(TYPE_PIPE)
static const char* kType = "pipe";
#else
static const char* kType = "?";
#endif

int main() {
    logMsg(std::string("client_") + kType +
           ": standalone stub (variant 1 is the 'related' configuration, "
           "the kid is forked by the host binary).");
    return 0;
}
