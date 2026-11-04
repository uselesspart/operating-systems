#pragma once
#include "Conn.h"
#include "SharedSems.h"
#include <memory>
#include <string>

class ConnFifo : public Conn {
public:
    // sems - общая память с базовым путём FIFO (channel_name)
    // isHost=true: создаёт файлы и владеет ими; isHost=false: открывает
    ConnFifo(SharedSems* sems, bool isHost);
    ~ConnFifo() override;

    bool Write(const void* buf, size_t count) override;
    bool Read(void* buf, size_t count) override;

    // Создаёт два FIFO (h2c и c2h) до fork и пишет базовый путь в sems
    static void preFork(SharedSems* sems);
    static std::unique_ptr<ConnFifo> createHost(SharedSems* sems);
    static std::unique_ptr<ConnFifo> createClient(SharedSems* sems);

private:
    int fd_w_ = -1;
    int fd_r_ = -1;
    std::string path_w_;
    std::string path_r_;
    bool owner_;  // true у хоста - он удаляет файлы unlink в деструкторе
};