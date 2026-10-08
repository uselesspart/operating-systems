#pragma once

#include <chrono>
#include <vector>

struct Task;

class Notifier{
    private:
        std::vector<Task> tasks;
        std::chrono::sys_seconds lastCheck;

        void show(const Task& task);

    public:
        Notifier(const std::vector<Task>& tasks);

        void changeConfig(const std::vector<Task>& tasks);
        void check();
};
