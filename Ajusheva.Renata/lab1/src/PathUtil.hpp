#pragma once

#include <optional>
#include <string>


namespace diskmond::path {


std::string join(const std::string& dir, const std::string& name);


std::string parentOf(const std::string& path);


std::string nameOf(const std::string& path);


bool isWithin(const std::string& path, const std::string& base);


std::string normalize(const std::string& path);


std::string absolute(const std::string& path);


std::optional<std::string> canonical(const std::string& path);


std::string resolve(const std::string& path);

bool isDirectory(const std::string& path);
bool isRegularFile(const std::string& path);

}  
