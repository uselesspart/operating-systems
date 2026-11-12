#include "notifier.h"

#include "types/task.h"

#include <signal.h>
#include <syslog.h>
#include <unistd.h>

namespace {

std::chrono::sys_seconds now(){
    return std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
}

}

Notifier::Notifier(const std::vector<Task>& tasks) : tasks(tasks), lastCheck(now()){}

void Notifier::changeConfig(const std::vector<Task>& tasks){
    this->tasks = tasks;
}

void Notifier::check(){
    auto current = now();
    for (const auto& task : tasks) {
        auto next = nextOccurrence(task, lastCheck);
        if (next && *next <= current) show(task);
    }
    lastCheck = current;
}

void Notifier::show(const Task& task){
    syslog(LOG_INFO, "reminder: %s", task.text.c_str());

    pid_t pid = fork();
    if (pid < 0) syslog(LOG_ERR, "fork failed: %m");
    if (pid == 0) {
        signal(SIGCHLD, SIG_DFL);
        execlp("xterm", "xterm", "-hold", "-e", "echo", task.text.c_str(), nullptr);
        syslog(LOG_ERR, "failed to start xterm: %m");
        _exit(1);
    }
}
