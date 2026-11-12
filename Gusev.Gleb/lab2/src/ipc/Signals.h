#pragma once

namespace wolf::hostsignals {

// Блокирует SIGINT/SIGTERM до создания потоков: их принимает отдельный поток хоста.
void blockHostSignals();

}
