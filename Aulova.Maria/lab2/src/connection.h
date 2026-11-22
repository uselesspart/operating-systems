#pragma once

#include <sys/types.h>

#include <memory>
#include <string>

#include "message.h"

struct SharedSync;

constexpr int kSyncTimeoutMs = 5000;
constexpr unsigned kSlots = 8;

enum class SendResult { Ok, Busy, Closed, Error };
enum class RecvResult { Message, Empty, Closed, Error };

class Connection {
public:
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    virtual ~Connection();

    bool connect(Party self, int timeoutMs = kSyncTimeoutMs);
    SendResult send(const WireMessage& message);
    RecvResult receive(WireMessage& message);

    virtual int readFd() const = 0;
    const std::string& error() const { return m_error; }

protected:
    Connection();

    virtual bool hostCreate() = 0;
    virtual bool clientOpen() = 0;
    virtual bool hostFinish() { return true; }
    virtual SendResult writeFrame(const WireMessage& message) = 0;
    virtual RecvResult readFrame(WireMessage& message) = 0;

    bool isOwner() const;
    void setError(const std::string& text) { m_error = text; }
    static std::string errnoMessage(const std::string& what);

private:
    SharedSync* m_sync = nullptr;
    pid_t m_ownerPid;
    Party m_self = Party::Host;
    bool m_connected = false;
    std::string m_error;
};

std::unique_ptr<Connection> createConnection();
