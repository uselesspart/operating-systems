#include "daemon.hpp"

#include <algorithm>
#include <array>
#include <barrier>
#include <thread>
#include <type_traits>
#include <vector>

static_assert(!std::is_default_constructible_v<application::Daemon>);
static_assert(!std::is_copy_constructible_v<application::Daemon>);
static_assert(!std::is_copy_assignable_v<application::Daemon>);
static_assert(!std::is_move_constructible_v<application::Daemon>);

int main() {
  // Concurrent first access exercises static initialization under TSan.
  std::array<application::Daemon *, 8> instances {};
  std::barrier start(static_cast<std::ptrdiff_t>(instances.size()));
  std::vector<std::thread> threads;
  for (std::size_t index = 0; index < instances.size(); ++index) {
    threads.emplace_back([&, index] {
      start.arrive_and_wait();
      instances[index] = &application::Daemon::instance();
    });
  }
  for (auto &thread : threads) {
    thread.join();
  }
  return std::all_of(instances.begin(), instances.end(), [&](const auto *instance) {
    return instance == &application::Daemon::instance();
  }) ? 0 : 1;
}
