#pragma once

#include <optional>
#include <string>

#include <sys/types.h>

namespace diskmond {


class PidFile {
  public:
    explicit PidFile(std::string location = {});

    const std::string& location() const noexcept { return location_; }

    
    std::optional<pid_t> load() const;

    
    bool store(pid_t pid, std::string& error) const;


    void release(pid_t owner) const;

  private:
    std::string location_;
};


std::string defaultPidFileLocation();

} 
