#pragma once
#include "Conn.h"
#include "SharedSems.h"
#include <mqueue.h>
#include <memory>
#include <string>

class ConnMq : public Conn {
public:
    // sems - общая память, откуда берётся имя очереди (channel_name)
    // isHost=true: создаёт очереди; isHost=false: открывает существующие
    ConnMq(SharedSems* sems, bool isHost);
    ~ConnMq() override;

    bool Write(const void* buf, size_t count) override;
    bool Read(void* buf, size_t count) override;

    // Вызывается до fork: записывает имя канала в sems->channel_name
    static void preFork(SharedSems* sems);
    static std::unique_ptr<ConnMq> createHost(SharedSems* sems);
    static std::unique_ptr<ConnMq> createClient(SharedSems* sems);

private:
    mqd_t mq_send_ = (mqd_t)-1;
    mqd_t mq_recv_ = (mqd_t)-1;
    std::string name_send_;
    std::string name_recv_;
    bool owner_;  // true у хоста - он удаляет очереди в деструкторе
};