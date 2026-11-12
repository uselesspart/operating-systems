
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <thread>
#include <chrono>
#include <limits.h>
#include <functional>
#include <cerrno>

namespace fs = std::filesystem;

class Daemon {
public:
    static Daemon& getInstance() {
        static Daemon instance;
        return instance;
    }

    void run(const std::string& configPath) {
        this->configPath = configPath;
        setup();
        loop();
    }

private:
    Daemon() {}
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    std::string configPath;
    std::string pidFilePath = "/tmp/lab1_daemon.pid";
    int interval = 20;
    std::vector<std::pair<std::string, int>> config;
    volatile sig_atomic_t reloadFlag = 0;
    volatile sig_atomic_t terminateFlag = 0;

    static void signalHandler(int sig) {
        if (sig == SIGHUP) Daemon::getInstance().reloadFlag = 1;
        else if (sig == SIGTERM) Daemon::getInstance().terminateFlag = 1;
    }

    // проверяем, что процесс - это наш демон
    bool isOurDaemon(pid_t pid) {
        std::error_code ec;
        fs::path ownExecutable = fs::read_symlink("/proc/self/exe", ec);
        if (ec) return false;

        fs::path targetExecutable = fs::read_symlink(
            "/proc/" + std::to_string(pid) + "/exe", ec
        );
        if (ec) return false;

        return ownExecutable == targetExecutable;
    }

    bool isProcessRunning(pid_t pid) {
        std::ifstream statFile(
            "/proc/" + std::to_string(pid) + "/stat"
        );

        if (!statFile.is_open()) return false;

        std::string line;
        std::getline(statFile, line);

        std::size_t pos = line.rfind(')');
        if (pos == std::string::npos || pos + 2 >= line.size()) {
            return false;
        }

        char state = line[pos + 2];
        return state != 'Z' && state != 'X';
    }

    bool waitForProcessExit(pid_t pid, int timeoutSeconds) {
        int checks = timeoutSeconds * 10;

        for (int i = 0; i < checks; ++i) {
            if (!isProcessRunning(pid)) return true;

            std::this_thread::sleep_for(
                std::chrono::milliseconds(100)
            );
        }

        return !isProcessRunning(pid);
    }

    void setup() {
        openlog("lab1_daemon", LOG_PID | LOG_CONS, LOG_USER);

        checkAndKillOldProcess();

        std::ofstream pidFile(pidFilePath);
        if (!pidFile.is_open()) {
            syslog(LOG_ERR, "Cannot write PID file: %s",
                   pidFilePath.c_str());
            closelog();
            std::exit(EXIT_FAILURE);
        }

        pidFile << getpid() << '\n';
        pidFile.close();

        syslog(LOG_INFO, "Daemon started, PID: %d", getpid());

        signal(SIGHUP, signalHandler);
        signal(SIGTERM, signalHandler);
        readConfig();
    }

    void checkAndKillOldProcess() {
        std::ifstream pidFile(pidFilePath);
        if (!pidFile.is_open()) return;

        pid_t oldPid = -1;
        if (!(pidFile >> oldPid)) return;
        pidFile.close();

        if (oldPid <= 0 || oldPid == getpid()) return;

        if (!isProcessRunning(oldPid)) return;

        // Проверяем, что это наш демон, а не посторонний процесс
        if (!isOurDaemon(oldPid)) {
            syslog(LOG_WARNING,
                   "PID %d in pid-file belongs to another process. Ignoring.",
                   oldPid);
            return;
        }

        syslog(LOG_INFO,
               "Old daemon (PID %d) is running. Sending SIGTERM.",
               oldPid);

        if (kill(oldPid, SIGTERM) == -1 && errno != ESRCH) {
            syslog(LOG_ERR, "Cannot send SIGTERM to PID %d: %s",
                   oldPid, strerror(errno));
            return;
        }

        if (waitForProcessExit(oldPid, 5)) return;

        if (isProcessRunning(oldPid) && isOurDaemon(oldPid)) {
            syslog(LOG_WARNING,
                   "Old daemon didn't finish in time, sending SIGKILL.");

            if (kill(oldPid, SIGKILL) == -1 && errno != ESRCH) {
                syslog(LOG_ERR, "Cannot send SIGKILL to PID %d: %s",
                       oldPid, strerror(errno));
                return;
            }

            if (!waitForProcessExit(oldPid, 5)) {
                syslog(LOG_ERR,
                       "Old daemon (PID %d) has not exited.", oldPid);
            }
        }
    }

    void readConfig() {
        config.clear();
        interval = 20;

        std::ifstream file(configPath);
        if (!file.is_open()) {
            syslog(LOG_ERR, "Cannot open config: %s",
                   configPath.c_str());
            return;
        }

        std::string line;

        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;

            std::istringstream iss(line);
            std::string first;
            iss >> first;

            if (first == "interval") {
                std::string valStr;

                if (iss >> valStr) {
                    try {
                        std::size_t pos = 0;
                        int val = std::stoi(valStr, &pos);

                        if (pos == valStr.size() && val > 0) {
                            interval = val;
                        } else {
                            syslog(LOG_WARNING,
                                   "Invalid interval %s, using 20.",
                                   valStr.c_str());
                        }
                    } catch (...) {
                        syslog(LOG_WARNING,
                               "Invalid interval format %s, using 20.",
                               valStr.c_str());
                    }
                }
            } else {
                int depth;

                if (iss >> depth) {
                    config.push_back({first, depth});
                }
            }
        }

        syslog(LOG_INFO,
               "Config reloaded. %zu entries, interval %d.",
               config.size(), interval);
    }

    void deleteSubfolders(const std::string& basePath, int maxDepth) {
        if (!fs::exists(basePath) || !fs::is_directory(basePath)) return;

        std::function<void(const std::string&, int)> traverse =
            [&](const std::string& currentPath, int currentDepth) {
                DIR* dir = opendir(currentPath.c_str());
                if (!dir) return;

                std::vector<std::string> subdirs;
                struct dirent* entry;

                while ((entry = readdir(dir)) != nullptr) {
                    std::string name = entry->d_name;
                    if (name == "." || name == "..") continue;

                    std::string fullPath = currentPath + "/" + name;
                    struct stat statbuf;

                    if (stat(fullPath.c_str(), &statbuf) == 0 &&
                        S_ISDIR(statbuf.st_mode)) {
                        subdirs.push_back(fullPath);
                    }
                }

                closedir(dir);

                for (const auto& dirPath : subdirs) {
                    if (currentDepth == maxDepth) {
                        // Удаляем только файлы внутри папки, но не саму папку
                        // И не трогаем вложенные папки (они глубже)
                        DIR* subdir = opendir(dirPath.c_str());

                        if (subdir) {
                            struct dirent* subentry;

                            while ((subentry = readdir(subdir)) != nullptr) {
                                std::string subname = subentry->d_name;
                                if (subname == "." || subname == "..") continue;

                                std::string subFullPath =
                                    dirPath + "/" + subname;
                                struct stat subStat;

                                if (stat(subFullPath.c_str(), &subStat) == 0 &&
                                    !S_ISDIR(subStat.st_mode)) {
                                    // Это файл - удаляем
                                    if (fs::remove(subFullPath)) {
                                        syslog(LOG_INFO, "Deleted file: %s",
                                               subFullPath.c_str());
                                    }
                                }
                            }

                            closedir(subdir);
                        }
                    } else if (currentDepth < maxDepth) {
                        // Идем глубже
                        traverse(dirPath, currentDepth + 1);
                    }
                }
            };

        if (maxDepth < 1) {
            syslog(LOG_WARNING, "Invalid depth %d for %s.",
                   maxDepth, basePath.c_str());
            return;
        }

        traverse(basePath, 1);
    }

    void doWork() {
        for (const auto& entry : config) {
            syslog(LOG_INFO, "Processing: %s (depth: %d)",
                   entry.first.c_str(), entry.second);
            deleteSubfolders(entry.first, entry.second);
        }
    }

    void loop() {
        while (!terminateFlag) {
            if (reloadFlag) {
                reloadFlag = 0;
                readConfig();
            }

            doWork();

            for (int i = 0; i < interval; ++i) {
                if (terminateFlag || reloadFlag) break;

                std::this_thread::sleep_for(
                    std::chrono::seconds(1)
                );
            }
        }

        syslog(LOG_INFO, "Daemon is terminating.");
        closelog();

        std::ifstream pidFile(pidFilePath);

        if (pidFile.is_open()) {
            pid_t currentPid;
            pidFile >> currentPid;
            pidFile.close();

            if (currentPid == getpid()) {
                fs::remove(pidFilePath);
            }
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0]
                  << " <config_file>" << std::endl;
        return 1;
    }

    char resolvedPath[PATH_MAX];
    std::string finalConfigPath = argv[1];

    if (realpath(argv[1], resolvedPath) != nullptr) {
        finalConfigPath = resolvedPath;
    }

    pid_t pid = fork();

    if (pid < 0) exit(EXIT_FAILURE);

    if (pid > 0) {
        std::cout << "Daemon started in background. PID: "
                  << pid << std::endl;
        exit(EXIT_SUCCESS);
    }

    umask(0);

    if (setsid() < 0) exit(EXIT_FAILURE);

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    Daemon::getInstance().run(finalConfigPath);

    return 0;
}
