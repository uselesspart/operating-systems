#pragma once

#include <signal.h>

namespace lab2::handshake {

// Вызывать в главном потоке до создания Qt и рабочих потоков.
void blockHandshakeSignals();

void requireHandshakeSignalsBlocked();

} // namespace lab2::handshake
