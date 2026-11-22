#pragma once

#include <chrono>

namespace wolf {

using namespace std::chrono_literals;

// Любое ожидание канала, семафора или сигнала ограничено 5 секундами.
constexpr std::chrono::milliseconds kIpcTimeout = 5s;

// Ход волка: столько ждём число от пользователя, затем бросаем случайное.
constexpr std::chrono::milliseconds kDefaultTurn = 3s;

// Сбор бросков раунда: меньше kIpcTimeout, чтобы один козлёнок не задерживал всех.
constexpr std::chrono::milliseconds kRoundCollect = 3s;

// Верхняя граница числа козлят: под неё заранее выделен массив локальных семафоров.
inline constexpr int kMaxKids = 64;

// Сколько ждём завершения потомков при остановке хоста.
constexpr std::chrono::milliseconds kStopGrace = 2s;

}
