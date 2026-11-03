#include "test_protocol.h"
#include "Protocol.h"
#include <cassert>
#include <cstring>

void testProtocol() {
    static_assert(sizeof(HostToClient) == 8, "HostToClient must be 8 bytes");
    static_assert(sizeof(ClientToHost) == 8, "ClientToHost must be 8 bytes");

    // Round-trip HostToClient с типом WolfNumber
    HostToClient h{};
    h.type  = WolfNumber;
    h.value = 42;
    unsigned char buf[sizeof(h)];
    std::memcpy(buf, &h, sizeof(h));

    HostToClient h2{};
    std::memcpy(&h2, buf, sizeof(buf));
    assert(h2.type  == WolfNumber);
    assert(h2.value == 42);

    // Round-trip HostToClient с типом Status
    HostToClient s{};
    s.type  = Status;
    s.value = 0;
    std::memcpy(buf, &s, sizeof(s));
    HostToClient s2{};
    std::memcpy(&s2, buf, sizeof(buf));
    assert(s2.type  == Status);
    assert(s2.value == 0);

    // Round-trip ClientToHost
    ClientToHost c{};
    c.type  = KidNumber;
    c.value = 17;
    unsigned char buf2[sizeof(c)];
    std::memcpy(buf2, &c, sizeof(c));
    ClientToHost c2{};
    std::memcpy(&c2, buf2, sizeof(buf2));
    assert(c2.type  == KidNumber);
    assert(c2.value == 17);
}