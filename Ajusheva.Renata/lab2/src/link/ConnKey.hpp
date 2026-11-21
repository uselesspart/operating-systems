#pragma once

#include <sys/types.h>

namespace chatlab {

struct ConnKey {
    pid_t host;
    int slot;
};

}
