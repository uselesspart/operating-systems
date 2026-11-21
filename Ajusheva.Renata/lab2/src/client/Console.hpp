#pragma once

#include "chat/Packets.hpp"

#include <ostream>

#include <sys/types.h>
#include <string>
#include <vector>

namespace chatlab {

class Console {
  public:
    explicit Console(std::ostream& out) : out_(out) {}

    void setViewer(int id) { viewer_ = id; }

    void greeting(pid_t host, const char* kind, int id, const std::string& name);
    void show(const packets::Reply& reply);
    void roster();
    void help();
    void problem(const std::string& text);
    void info(const std::string& text);

  private:
    std::ostream& out_;
    int viewer_ = 0;
    std::vector<Member> roster_;
};

}
