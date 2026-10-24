#include "../headers/pidfile.hpp"

#include <chrono>
#include <csignal>
#include <fstream>
#include <thread>

PidFile::PidFile(std::filesystem::path path) : pid_path_(std::move(path)) {}

PidFile::~PidFile() { release(); }

pid_t PidFile::read_pid() const {

    std::ifstream file(pid_path_);
    pid_t pid = 0;
    if (!(file >> pid)) return 0;

    return pid;
}

//Правки к замечанию 2
std::string read_comm(const std::string& who) {

    std::ifstream f("/proc/" + who + "/comm");
    std::string name;
    std::getline(f, name);
    
    return name;
}


bool PidFile::is_process_running(pid_t pid) {

    std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
    if (!stat.is_open()) return false;
    std::string content;
    std::getline(stat, content);

    size_t p = content.rfind(')');
    if (p == std::string::npos || p + 2 >= content.size()) return false;

    return content[p + 2] != 'Z';
}

bool PidFile::is_same_program(pid_t pid) {

    std::string other = read_comm(std::to_string(pid));
    std::string self  = read_comm("self");
    if (other.empty() || self.empty()) return true;
 
    return other == self;
}
bool PidFile::terminate_previous(int timeout_ms) {
    pid_t old_pid = read_pid();
    if (old_pid <= 0 || old_pid == getpid()) return true;
    if (!is_process_running(old_pid) || !is_same_program(old_pid)) return true; 

    if (kill(old_pid, SIGTERM) != 0) return false;

    for (int waited = 0; waited < timeout_ms; waited += 100) {

        if (!is_process_running(old_pid)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    }

    return !is_process_running(old_pid);
}

bool PidFile::write_pid() {

    std::error_code ec;

    if (pid_path_.has_parent_path())
    {
        std::filesystem::create_directories(pid_path_.parent_path(), ec);
    }
        

    std::ofstream file(pid_path_, std::ios::trunc);
    if (!file.is_open()) return false;

    file << getpid() << "\n";
    file.close();
    if (!file) return false;
    is_acquired_ = true;

    return true;
}

void PidFile::release() {
    
    if (!is_acquired_) return;

    if (read_pid() == getpid()) {
        std::error_code ec;
        std::filesystem::remove(pid_path_, ec);
    }

    is_acquired_ = false;
}
