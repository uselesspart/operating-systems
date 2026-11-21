#pragma once

#include "chat/Post.hpp"

#include <string>
#include <vector>

namespace chatlab::render {

std::string postLine(const Post& post, int viewer);
std::string rosterLine(const std::vector<Member>& roster);

}
