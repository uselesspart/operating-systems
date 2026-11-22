#include "memory_connection.hpp"

namespace wolf {
namespace {

class AnonymousConnection final : public MemoryConnection {
public:
    AnonymousConnection() { mapBuffers(); }

private:
    void openMemory(Side) override {}
};

}

std::unique_ptr<Connection> makeConnection()
{
    return std::make_unique<AnonymousConnection>();
}

}
