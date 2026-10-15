#include "PidFile.hpp"

#include <fstream>
#include <cerrno>
#include <cstdlib>
#include <string>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>
#include <csignal>
#include <sys/file.h>

void PidFile::acquireLock(){
    if(fd_ != -1){
        throw std::runtime_error("PID file is already opened");
    }

    //Открытие файла (или создание)
    fd_ = open(PidFile::PATH, O_RDWR | O_CREAT | O_CLOEXEC, 0644);

    if(fd_ == -1) {
        throw std::runtime_error(
            std::string("PID file could not be opened or created: ")
            + std::strerror(errno)
            );
    }

    //Попытка установить эксклюзивную блокировку
    if (flock(fd_, LOCK_EX | LOCK_NB) == 0){
        //Успешно получен lock
        return; 
    }

    //flock() с ошибкой
    if (errno != EWOULDBLOCK){
        int error = errno;
        close(fd_);
        fd_ = -1;
        throw std::runtime_error(
            std::string("Cannot lock PID file: ")
            + std::strerror(error)
            );
    }

    //Файл заблокирован другим процессом errno == EWOULDBLOCK
    
    //Завершаем предыдущий процесс
    terminatePreviousInstance();

    if (flock(fd_, LOCK_EX) == -1){
        int error = errno;
        close(fd_);
        fd_ = -1;
        throw std::runtime_error(
            std::string("Cannot acquire PID lock: ")
            + std::strerror(error)
            );
    }

    //Успешно получен lock
}
void PidFile::terminatePreviousInstance(){
    //Получение pid
    const pid_t pid = readPid();

    if (pid <= 0){
        throw std::runtime_error(
            "PID file is locked, but does not contain a valid PID"
        );
    }
    //Проверка существования процесса
    if (!processExists(pid)){
        throw std::runtime_error(
            "PID file is locked, but process does not exist"
        );
    }
    //Попытка завершить процесс
    if (kill(pid, SIGTERM) == -1){
        if(errno == ESRCH){
            return; // Процесс завершился между проверкой /proc и kill()
        }

        throw std::runtime_error(
            std::string("Cannot send SIGTERM")
            + std::strerror(errno)
        );
        
    }

}

void PidFile::writeCurrentPid(){
    // получение текущего демона
    pid_t pid = getpid();
    std::string pidString = std::to_string(pid);
    pidString += '\n';

    // сброс позиции в начало файла
    if (lseek(fd_, 0, SEEK_SET) == -1){
        throw std::runtime_error(
            std::string("Cannot seek PID file: ")
            + std::strerror(errno)
            );
    }
    // усечение файла до нуля перед записью
    if (ftruncate(fd_, 0) == -1){
        throw std::runtime_error(
            std::string("Cannot truncate PID file: ")
            + std::strerror(errno)
            );
    }
    
    //Запись всех байт (write может записать меньше или прерваться сигналом)
    const char* data = pidString.data();
    size_t total = pidString.size();
    size_t written = 0;

    while(written < total){
        ssize_t result = write(fd_, data + written, total - written);

        if(result == -1){
            if(errno == EINTR){
                continue; //Прервано сигналом — повторяем
            }
            throw std::runtime_error(
                std::string("Cannot write PID to file: ")
                + std::strerror(errno)
                );
        }

        if(result == 0){
            // для regular file не должно случаться
            throw std::runtime_error(
                "Cannot write PID to file: write returned 0"
            );
        }

        written += static_cast<size_t>(result);
    }

}

void PidFile::release() noexcept{
    if (fd_ == -1){
        return;
    }
    ftruncate(fd_,0);
    close(fd_);
    fd_ = -1;
}

pid_t PidFile::readPid() const{

    if(fd_ == -1){
        throw std::runtime_error("PID file is not opened");
    }

    //Чтение файла

    if(lseek(fd_, 0, SEEK_SET) == -1 ){
        throw std::runtime_error(
            std::string("Cannot seek PID file: ")
            + std::strerror(errno)
            );
    }

    char buffer[128];
    ssize_t bytes_read = read(fd_, buffer, sizeof(buffer) - 1);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        //Прочитанный PID из файла:  buffer 
        long v = std::strtol(buffer, nullptr, 10);
        if(v > 0){
            return static_cast<pid_t>(v);
        }
        else{
            throw std::runtime_error(
            "PID file contains invalid PID"
            );
        }

    } else if (bytes_read == 0) {
        // Файл пуст (новый процесс)
        return -1;
    } else {
        // Ошибка чтения файла
        throw std::runtime_error(
            std::string("Cannot read PID file:")
            + std::strerror(errno)
        );
    }

}

bool PidFile::processExists(pid_t pid) const{

    if(pid <= 0){
        return false;
    }

    const std::filesystem::path processPath =
            std::filesystem::path("/proc") / std::to_string(pid);

    return std::filesystem::exists(processPath);
}

PidFile::~PidFile()noexcept{

    if(fd_ != -1){
        close(fd_);
    }
}
