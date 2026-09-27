#pragma once

#include <string>
#include <vector>
#include <map>

namespace monitor {

class Worker {
public:
    Worker();
    ~Worker();

    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    void init(const std::vector<std::string>& dirs);

    void pollEvents();

private:
    void cleanup();

    int inotifyFd_;
    std::map<int, std::string> watchMap_; 
};

} // namespace monitor