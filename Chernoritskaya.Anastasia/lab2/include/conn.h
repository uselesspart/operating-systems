#pragma once
#include <cstddef>
#include <string>

class Conn {
public:
    explicit Conn(const std::string& id, bool create) : m_id(id), m_create(create) {}
    virtual ~Conn() = default;

    virtual bool Read(void* buf, size_t count) = 0;
    virtual bool Write(const void* buf, size_t count) = 0;
    virtual bool Handshake() = 0;
    virtual int  GetPeerPid() const { return -1; }

protected:
    std::string m_id;
    bool m_create;
};

Conn* createConn(const std::string& id, bool create);

// структура сообщения для всех типов IPC
struct WireMessage {
    enum Type : int { BROADCAST = 1, PRIVATE = 2, ACK = 3 };
    int  type;
    char sender[32];
    char recipient[32];
    char content[512];
    long long timestamp_ms;
};