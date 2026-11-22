#pragma once

#include <string>

std::string pidFilePath();
bool stopRunningInstance(std::string& error);
bool writePidFile(std::string& error);
void removePidFile();
