#pragma once

#include <string>

// Журнал: строка с временем в stderr и сообщение в syslog. Потокобезопасен.
namespace wolf::log {

void init(const std::string& ident);
void info(const std::string& message);
void warn(const std::string& message);
void error(const std::string& message);

}
