#include "conn.hpp"
#include "system.hpp"

Conn::Conn(bool host, const std::string& name, int hostSocket, int clientSocket)
    : in_(host ? hostSocket : clientSocket), out_(in_), host_(host), name_(name) {
    close(host ? clientSocket : hostSocket);
}
Conn::~Conn() { close(in_); }
void Conn::Read(void* buf, std::size_t count) { transfer(in_, buf, count, false); }
void Conn::Write(const void* buf, std::size_t count) { transfer(out_, const_cast<void*>(buf), count, true); }
