#pragma once

#include "wolf/conn.hpp"
#include "wolf/shared_state.hpp"

#include <memory>

namespace wolf {

// Вызывается один раз до fork: подготавливает имя канала или пару сокетов.
void PrepareTransport(SharedState* state);

std::unique_ptr<Conn> OpenTransport(Role role, SharedState* state);

}
