#pragma once

#include <vector>

#include "AppConfig.hpp"

struct inotify_event;

namespace application
{

class Monitor {
private:
    int fd_{-1};
    AppConfig config_;
    std::vector<int> wds_;

    void clear();
    void process_event(const inotify_event* event);
public:
    explicit Monitor(AppConfig config);
    
    Monitor(const Monitor&) = delete;
    Monitor& operator=(const Monitor&) = delete;

    Monitor(Monitor&&) = delete;
    Monitor& operator=(Monitor&&) = delete;

    ~Monitor();

    int fd() const { return fd_; }
    bool read_events();
};
    
} // namespace application
