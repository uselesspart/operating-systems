#pragma once

#include <string>

namespace chatlab::journal {

enum class Echo { Silent, Stderr };

void open(const char* ident, Echo echo);
void close();

void info(const std::string& text);
void warn(const std::string& text);
void fail(const std::string& text);

}
