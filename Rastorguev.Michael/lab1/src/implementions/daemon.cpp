#include "../headers/daemon.hpp"
#include "../headers/pidfile.hpp"

#include <csignal>
#include <ctime>
#include <fcntl.h>
#include <iostream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

namespace {
constexpr const char* kPidPath = "/tmp/schedule_daemon.pid";
constexpr const char* kIdent   = "schedule_daemon";
constexpr time_t kGrace = 60;

std::string event_key(const Event& e) { return std::to_string(e.when) + "|" + e.text; }
}

int Daemon::run(const std::string& config) {

    std::error_code ec;
    config_path_ = std::filesystem::absolute(config, ec);
    if (ec) {
        std::cerr << "Ошибка: не удалось определить путь к конфигу\n";
        return 1;
    }

    openlog(kIdent, LOG_PID, LOG_DAEMON);

    std::string err;
    if (!Config::load(config_path_, cfg_, err)) {
        std::cerr << "Ошибка: " << err << "\n";
        syslog(LOG_ERR, "Ошибка: %s", err.c_str());
        closelog();
        return 1;
    }
    prepare_events();

    PidFile pid_file(kPidPath);
    if (!pid_file.terminate_previous()) {
        std::cerr << "Ошибка: не удалось завершить предыдущую копию демона\n";
        syslog(LOG_ERR, "Не удалось завершить предыдущую копию демона");
        closelog();
        return 1;
    }

    if (!daemonize()) {
        syslog(LOG_ERR, "Ошибка демонизации");
        closelog();
        return 1;
    }

    closelog();
    openlog(kIdent, LOG_PID, LOG_DAEMON);

    if (!pid_file.write_pid()) {
        syslog(LOG_ERR, "Не удалось записать pid-файл %s", kPidPath);
        closelog();
        return 1;
    }

    syslog(LOG_INFO, "Демон запущен (конфиг: %s, событий: %zu, интервал: %u с)",
           config_path_.c_str(), cfg_.events.size(), cfg_.interval);

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGHUP);
    sigprocmask(SIG_BLOCK, &set, nullptr);

    while (running_) {
        tick();

        timespec ts{};
        ts.tv_sec = cfg_.interval;
        int sig = sigtimedwait(&set, nullptr, &ts);
        if (sig == SIGTERM)      on_sigterm();
        else if (sig == SIGHUP)  on_sighup();
    }

    pid_file.release();
    closelog();
    return 0;
}

bool Daemon::daemonize() {
    
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return false; }
    if (pid > 0) _exit(EXIT_SUCCESS);

    if (setsid() < 0) return false;

    umask(0);
    if (chdir("/") != 0) return false;

    int fd = open("/dev/null", O_RDWR);
    if (fd < 0) return false;
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO) close(fd);
    return true;
}


void Daemon::prepare_events() {
    time_t now = time(nullptr);
    std::vector<Event> ready;

    for (Event ev : cfg_.events) {
        if (ev.repeat) {

            //Исправление недочёта 3

            auto it = last_fired_.find(event_key(ev));

            time_t floor = (it != last_fired_.end()) ? it->second + 1 : now - kGrace;
            while (ev.when < floor) ev.when = advance_time(ev.when, ev.repeat);
            while (ev.when < now - kGrace) ev.when = advance_time(ev.when, ev.repeat);

        } 
        else 
        {
            if (fired_.count(event_key(ev))) continue;
            if (ev.when < now - kGrace) {
                syslog(LOG_INFO, "Событие пропущено (время прошло): %s", ev.text.c_str());
                continue;
            }
        }
        ready.push_back(ev);
    }
    cfg_.events = std::move(ready);
}

void Daemon::tick() {
    while (waitpid(-1, nullptr, WNOHANG) > 0) {}

    time_t now = time(nullptr);
    for (auto it = cfg_.events.begin(); it != cfg_.events.end();) {
        if (it->when > now) 
        { 
            it++; 
            continue; 
        }

        syslog(LOG_INFO, "Напоминание: %s", it->text.c_str());
        show(it->text);

        if (it->repeat) {

            //К исправлению 3
            last_fired_[event_key(*it)] = it->when;

            while (it->when <= now) it->when = advance_time(it->when, it->repeat);
            it++;
        } else {
            fired_.insert(event_key(*it));
            it = cfg_.events.erase(it);
        }
    }
}

void Daemon::on_sighup() {
    Config fresh;
    std::string err;
    if (!Config::load(config_path_, fresh, err)) {
        syslog(LOG_ERR, "SIGHUP: %s. Остаётся прежняя конфигурация", err.c_str());
        return;
    }
    cfg_ = std::move(fresh);
    prepare_events();
    syslog(LOG_INFO, "SIGHUP: конфигурация перечитана (событий: %zu, интервал: %u с)",
           cfg_.events.size(), cfg_.interval);
}

void Daemon::on_sigterm() {
    syslog(LOG_INFO, "SIGTERM: демон завершает работу");
    running_ = false;
}


void Daemon::show(const std::string& text) {
    pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "fork не удался");
        return;
    }
    if (pid > 0) return;


    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, nullptr);
    setsid();
    setenv("DISPLAY", ":0", 0);

    //Видел, что у коллег, которым зачли лабораторную, вызывается именно такой эмулятор
    execlp("xterm", "xterm", "-hold", "-e", "echo", text.c_str(), nullptr);

    //Исправление недочёта 1
    syslog(LOG_ERR, "не удалось запустить xterm");
    _exit(127);

}