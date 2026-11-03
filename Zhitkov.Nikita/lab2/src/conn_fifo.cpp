#include "ConnFifo.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>

// Создаёт два именованных канала до fork и записывает базовый путь в разделяемую память. Клиент после fork возьмёт этот путь оттуда
void ConnFifo::preFork(SharedSems* sems) {
    std::snprintf(sems->channel_name, sizeof(sems->channel_name),
                  "/tmp/wolf_%d", static_cast<int>(getpid()));

    const std::string base = sems->channel_name;
    const std::string h2c = base + "_h2c";
    const std::string c2h = base + "_c2h";

    if (mkfifo(h2c.c_str(), 0666) != 0 && errno != EEXIST) {
        throw std::runtime_error(std::string("mkfifo h2c: ") + std::strerror(errno));
    }
    if (mkfifo(c2h.c_str(), 0666) != 0 && errno != EEXIST) {
        throw std::runtime_error(std::string("mkfifo c2h: ") + std::strerror(errno));
    }
}

ConnFifo::ConnFifo(SharedSems* sems, bool isHost) : owner_(isHost) {
    const std::string base = sems->channel_name;
    if (base.empty()) {
        throw std::runtime_error("ConnFifo: channel_name not initialized");
    }

    const std::string h2c = base + "_h2c";
    const std::string c2h = base + "_c2h";

    // Открываем FIFO уже после fork
    // Порядок open важен: writer открывает O_WRONLY, reader O_RDONLY
    if (isHost) {
        path_w_ = h2c;
        path_r_ = c2h;
        fd_r_ = open(path_r_.c_str(), O_RDONLY);
        if (fd_r_ < 0) {
            throw std::runtime_error(std::string("open fifo r: ") + std::strerror(errno));
        }
        fd_w_ = open(path_w_.c_str(), O_WRONLY);
        if (fd_w_ < 0) {
            throw std::runtime_error(std::string("open fifo w: ") + std::strerror(errno));
        }
    } else {
        path_w_ = c2h;
        path_r_ = h2c;
        fd_w_ = open(path_w_.c_str(), O_WRONLY);
        if (fd_w_ < 0) {
            throw std::runtime_error(std::string("open fifo w: ") + std::strerror(errno));
        }
        fd_r_ = open(path_r_.c_str(), O_RDONLY);
        if (fd_r_ < 0) {
            throw std::runtime_error(std::string("open fifo r: ") + std::strerror(errno));
        }
    }
}

ConnFifo::~ConnFifo() {
    if (fd_w_ >= 0) close(fd_w_);
    if (fd_r_ >= 0) close(fd_r_);
    // По требованию задания после окончания работы родитель (хост) удаляет файлы именованного канала
    if (owner_) {
        unlink(path_w_.c_str());
        unlink(path_r_.c_str());
    }
}

bool ConnFifo::Write(const void* buf, size_t count) {
    // write в FIFO может записать меньше count байт - циклически догоняем
    const char* p = static_cast<const char*>(buf);
    size_t done = 0;
    while (done < count) {
        ssize_t n = write(fd_w_, p + done, count - done);
        if (n <= 0) return false;
        done += static_cast<size_t>(n);
    }
    return true;
}

bool ConnFifo::Read(void* buf, size_t count) {
    // Аналогично: read может вернуть меньше - читаем до count
    char* p = static_cast<char*>(buf);
    size_t done = 0;
    while (done < count) {
        ssize_t n = read(fd_r_, p + done, count - done);
        if (n <= 0) return false;
        done += static_cast<size_t>(n);
    }
    return true;
}

std::unique_ptr<ConnFifo> ConnFifo::createHost(SharedSems* sems) {
    return std::make_unique<ConnFifo>(sems, true);
}

std::unique_ptr<ConnFifo> ConnFifo::createClient(SharedSems* sems) {
    return std::make_unique<ConnFifo>(sems, false);
}