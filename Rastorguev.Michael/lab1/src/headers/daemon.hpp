#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <map>

#include "config.hpp"

class Daemon
{
  public:
    static Daemon& Instance()
    {
        static Daemon s;
        return s;
    }

    int run(const std::string& config);

  private:
    Daemon() {}
    ~Daemon() {}

    Daemon(Daemon const&) = delete;
    Daemon& operator=(Daemon const&) = delete;

    bool daemonize();
    void prepare_events();
    void tick();

    void on_sighup();
    void on_sigterm();

    void show(const std::string& text);
    
    std::filesystem::path config_path_;
    Config cfg_;
    std::set<std::string> fired_;
    bool running_ = true;

    //К исправлению 3
    std::map<std::string, time_t> last_fired_;
};
