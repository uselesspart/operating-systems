#pragma once

#include "chat/Post.hpp"

#include <string>
#include <vector>

namespace chatlab {

class HostEvents {
  public:
    virtual ~HostEvents() = default;

    virtual void onStatus(const std::string& text) = 0;
    virtual void onRoster(const std::vector<Member>& roster) = 0;
    virtual void onPost(const Post& post) = 0;
    virtual void onQuitRequest() = 0;
};

}
