#pragma once

#include <string>


namespace diskmond::journal {


enum class Echo { Off, Stderr };

void open(Echo echo);
void close();


void status(int severity, const std::string& text);

void fileEvent(const std::string& text);

}  
