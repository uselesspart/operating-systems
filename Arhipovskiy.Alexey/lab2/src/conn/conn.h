#ifndef CHAT_CONN_H
#define CHAT_CONN_H

#include "ipc/ConnId.h"

#include <cstddef>
#include <memory>

namespace chat {

// Two-way byte stream between the host and one client.
// Every conn_<type>.cpp implements this class; each executable links exactly one of them.
//
// create == true (host): creates the channel; the client attaches after the handshake,
// and the host's first Read/Write waits up to kIpcTimeout for it.
// create == false (client): attaches to an existing channel, throws std::system_error on failure.
class Conn {
public:
    Conn(const ConnId& id, bool create);
    ~Conn();

    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;

    // Transfer exactly count bytes; false if the peer is gone or the channel failed
    bool Read(void* buf, std::size_t count);
    bool Write(const void* buf, std::size_t count);

    static const char* typeCode();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}

#endif // CHAT_CONN_H
