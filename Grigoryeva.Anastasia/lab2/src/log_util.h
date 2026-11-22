#pragma once
#include <string>

std::string Timestamp();  // "ЧЧ:ММ:СС"
// Потокобезопасная запись строки журнала в stdout: "[время] [кто] сообщение"
void LogLine(const std::string& who, const std::string& msg);
