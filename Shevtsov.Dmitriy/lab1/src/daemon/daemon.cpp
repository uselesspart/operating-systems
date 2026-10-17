#include "daemon.h"

#include "notifier/notifier.h"
#include "parser/parser.h"
#include "types/task.h"

#include <filesystem>
#include <fstream>

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

namespace {

const char* const PID_FILE = "/tmp/reminder.pid";
const unsigned int INTERVAL = 10;

volatile sig_atomic_t gotHup = 0;
volatile sig_atomic_t gotTerm = 0;

void onSignal(int sig){
    if (sig == SIGHUP) gotHup = 1;
    if (sig == SIGTERM) gotTerm = 1;
}

pid_t readPid(){
    pid_t pid = 0;
    std::ifstream(PID_FILE) >> pid;
    return pid;
}

std::string readLine(const std::string& path){
    std::string line;
    std::getline(std::ifstream(path), line);
    return line;
}

}

Daemon::Daemon() = default;
Daemon::~Daemon() = default;

Daemon& Daemon::instance(){
    static Daemon daemon;
    return daemon;
}

int Daemon::run(const std::string& configFile){
    openlog("reminder", LOG_PID | LOG_PERROR, LOG_DAEMON);

    configPath = std::filesystem::absolute(configFile);
    if (!reloadConfig()) return EXIT_FAILURE;

    daemonize();
    signal(SIGHUP, onSignal);
    signal(SIGTERM, onSignal);
    signal(SIGCHLD, SIG_IGN);
    takePidFile();
    syslog(LOG_INFO, "started, config: %s", configPath.c_str());

    Notifier notifier(tasks);
    while (!gotTerm) {
        if (gotHup) {
            gotHup = 0;
            syslog(LOG_INFO, "SIGHUP received, reloading config");
            if (reloadConfig()) notifier.changeConfig(tasks);
        }
        notifier.check();
        sleep(INTERVAL);
    }

    syslog(LOG_INFO, "SIGTERM received, exiting");
    if (readPid() == getpid()) unlink(PID_FILE);
    closelog();
    return EXIT_SUCCESS;
}

bool Daemon::reloadConfig(){
    try {
        tasks = loadConfig(configPath);
        syslog(LOG_INFO, "config loaded: %zu events", tasks.size());
        return true;
    } catch (const std::exception& e) {
        syslog(LOG_ERR, "config error: %s", e.what());
        return false;
    }
}

void Daemon::daemonize(){
    pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "fork failed: %m");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) exit(EXIT_SUCCESS);

    setsid();
    umask(0);
    if (chdir("/") != 0) syslog(LOG_ERR, "chdir failed: %m");

    int fd = open("/dev/null", O_RDWR);
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO) close(fd);
}

void Daemon::takePidFile(){
    pid_t old = readPid();
    std::string proc = "/proc/" + std::to_string(old);

    if (old > 0 && std::filesystem::exists(proc) && readLine(proc + "/comm") == readLine("/proc/self/comm")) {
        syslog(LOG_INFO, "stopping previous instance, pid %d", old);
        kill(old, SIGTERM);
        for (int i = 0; i < 50 && std::filesystem::exists(proc); ++i) usleep(100000);
    }

    std::ofstream(PID_FILE) << getpid() << '\n';
}
