#include "Daemon.hpp"
#include "ConfigLoader.hpp"
#include "ReminderTask.hpp"

#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <syslog.h>
#include <ctime>

volatile sig_atomic_t Daemon::terminateRequested_ = 0;

volatile sig_atomic_t Daemon::reloadRequested_ = 0;

Daemon& Daemon::getInstance(){
    static Daemon instance;
    return instance;
}

int Daemon::run(const std::filesystem::path& configArgument){

    initializeConfiguration(configArgument);

    pidFile_.acquireLock();

    daemonize();

    try {

        redirectStandardStreams();
        
        pidFile_.writeCurrentPid();

        installSignalHandlers();

        syslog(
            LOG_INFO,
            "Daemon successfully started. PID: %d", static_cast<int>(getpid())
            );

        mainLoop();
    }
    catch (const std::exception& e){

        syslog(LOG_ERR, "Fatal daemon error: %s", e.what());

        pidFile_.release();

        return EXIT_FAILURE;

        }
       
    shutdown();
    return EXIT_SUCCESS;
}

void Daemon::mainLoop(){

    while (!terminateRequested_) // пока не поступит сигнал SIGTERM
    {
        if(reloadRequested_){ // поступил сигнал SIGHUP

            reloadRequested_ = 0;
            reloadConfig(); // 
        }

        // выполнение задачи
        try
        {
            reminderTask_.execute(config_);
        }
        catch(const std::exception& e)
        {
            syslog(
                LOG_ERR,
                "Reminder task error: %s",
                e.what()
            );
        }
        

        sleepInterval();
    }
}

void Daemon::sleepInterval(){

    timespec requested{};
    requested.tv_sec = config_.interval.count();
    requested.tv_nsec = 0;

    timespec remaining{};

    while (nanosleep(&requested, &remaining) == -1)
    {
        if (errno == EINTR)
        {
            // Разбудил сигнал
            if (terminateRequested_){
                return;
            }

            if (reloadRequested_){

                reloadRequested_ = 0;
                reloadConfig();
            }

            // досыпает оставшееся время.
            requested = remaining;
            continue;
        }

        throw std::runtime_error(
            std::string("nanosleep failed: ")
            + std::strerror(errno)
        );
    }
}

void Daemon::initializeConfiguration(const std::filesystem::path& configArgument){

    configPath_ = std::filesystem::canonical(configArgument);
    config_ = ConfigLoader::load(configPath_);

}

void Daemon::reloadConfig(){
    try
    {
        Config newConfig = ConfigLoader::load(configPath_);

        config_ = std::move(newConfig);

        syslog(
            LOG_INFO,
            "Configuration successfully reloaded"
        );
    }
    catch (const std::exception& e){
        syslog(
            LOG_ERR,
            "Cannot reload configuration: %s",
            e.what()
        );
    }
}

void Daemon::daemonize(){

    //Порождение процесса
    pid_t pid = fork();

    if(pid == -1){
        //fork error
        throw std::runtime_error(
            std::string("fork failed:")
            + std::strerror(errno));
        
    }

    if(pid > 0){
        //Старый процесс
        _exit(EXIT_SUCCESS);
    }

    //Задать демону новую группу процессов и сеанс, в котором демон будет ведущим процессом
    if(setsid() < 0){

      throw std::runtime_error(
            std::string("setsid failed:")
            + std::strerror(errno));
         
    }
    //Изменить рабочую директорию на root
    if(chdir("/") == -1){

        throw std::runtime_error(
            std::string("chdir failed:")
            + std::strerror(errno));
        
    }

}

void Daemon::redirectStandardStreams(){

    //открываем файловый дескриптор для перенаправления
    int nullFd = open("/dev/null", O_RDWR);

    if(nullFd == -1){
       throw std::runtime_error(
            std::string("open /dev/null failed: ")
            + std::strerror(errno));
         
    }

    // dup2 автоматически закрывает старый дескриптор
    if(dup2(nullFd, STDIN_FILENO)== -1){
        const int err = errno;
        if(nullFd > STDERR_FILENO) close(nullFd);
        
        throw std::runtime_error(
            std::string("dup2 stdin failed: ")
            +std::strerror(err)
        );

    }
    if(dup2(nullFd, STDOUT_FILENO)== -1){
        const int err = errno;
        if(nullFd > STDERR_FILENO) close(nullFd);
        
        throw std::runtime_error(
            std::string("dup2 stdout failed: ")
            +std::strerror(err)
        );
    }
    if(dup2(nullFd, STDERR_FILENO)== -1){
        const int err = errno;
        if(nullFd > STDERR_FILENO) close(nullFd);
        
        throw std::runtime_error(
            std::string("dup2 stderr failed: ")
            +std::strerror(err)
        );
    }

    if(nullFd > STDERR_FILENO) close(nullFd);

}

void Daemon::signalHandler(int signal){
    if (signal == SIGTERM){
        terminateRequested_ = 1;
    }
    if (signal == SIGHUP){
        reloadRequested_ = 1;
    }
}

void Daemon::installSignalHandlers(){

    struct sigaction action{};

    action.sa_handler = &Daemon::signalHandler;

    sigemptyset(&action.sa_mask);

    action.sa_flags = 0;

    if (sigaction(SIGTERM, &action, nullptr) == -1){
        throw std::runtime_error("SignalHandler error");
    }
    if (sigaction(SIGHUP, &action, nullptr) == -1){
        throw std::runtime_error("SignalHandler error");
    }

    struct sigaction childAction{};

    childAction.sa_handler = SIG_IGN;

    sigemptyset(&childAction.sa_mask);

    childAction.sa_flags = SA_NOCLDWAIT;


    if ( sigaction(SIGCHLD, &childAction, nullptr) == -1)
    {
        throw std::runtime_error(
            "Cannot install SIGCHLD handler"
        );
    }
}

void Daemon::shutdown() noexcept{

    syslog(
        LOG_INFO, 
        "Daemon is shutting down"
        );
        
    pidFile_.release();
}