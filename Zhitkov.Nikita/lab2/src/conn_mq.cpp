#include "ConnMq.h"
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {

// Должно совпадать с attr.mq_msgsize в конструкторе. mq_receive требует, чтобы буфер приёмника был >= mq_msgsize
constexpr size_t kMqMsgSize = 256;

}

// Формирует имя канала и сохраняет его в разделяемой памяти, чтобы клиент после fork взял оттуда же (а не из глобальной переменной)
void ConnMq::preFork(SharedSems* sems) {
    std::snprintf(sems->channel_name, sizeof(sems->channel_name),
                  "/wolf_%d", static_cast<int>(getpid()));
}

ConnMq::ConnMq(SharedSems* sems, bool isHost) : owner_(isHost) {
    const std::string base = sems->channel_name;
    if (base.empty()) {
        throw std::runtime_error("ConnMq: channel_name not initialized");
    }

    if (isHost) {
        // Хост создаёт обе очереди. Работа с mq_open идёт после fork -это соответствует требованию "как будто несвязанные процессы"
        name_send_ = base + "_h2c";
        name_recv_ = base + "_c2h";

        struct mq_attr attr{};
        attr.mq_maxmsg = 10;
        attr.mq_msgsize = kMqMsgSize;

        mq_send_ = mq_open(name_send_.c_str(), O_CREAT | O_RDWR, 0666, &attr);
        mq_recv_ = mq_open(name_recv_.c_str(), O_CREAT | O_RDWR, 0666, &attr);
    } else {
        // Клиент открывает уже созданные очереди (просто O_RDWR без O_CREAT)
        name_send_ = base + "_c2h";
        name_recv_ = base + "_h2c";
        mq_send_ = mq_open(name_send_.c_str(), O_RDWR);
        mq_recv_ = mq_open(name_recv_.c_str(), O_RDWR);
    }

    if (mq_send_ == (mqd_t)-1 || mq_recv_ == (mqd_t)-1) {
        throw std::runtime_error(std::string("mq_open: ") + std::strerror(errno));
    }
}

ConnMq::~ConnMq() {
    if (mq_send_ != (mqd_t)-1) mq_close(mq_send_);
    if (mq_recv_ != (mqd_t)-1) mq_close(mq_recv_);
    // Удаляет только хост - клиент не должен удалять очереди, иначе второй запуск хоста их не найдёт
    if (owner_) {
        mq_unlink(name_send_.c_str());
        mq_unlink(name_recv_.c_str());
    }
}

bool ConnMq::Write(const void* buf, size_t count) {
    return mq_send(mq_send_, static_cast<const char*>(buf), count, 0) == 0;
}

bool ConnMq::Read(void* buf, size_t count) {
    // mq_receive требует буфер >= mq_msgsize. Читаем во временный буфер фиксированного размера, потом копируем ровно count байт
    char tmp[kMqMsgSize];
    ssize_t r = mq_receive(mq_recv_, tmp, sizeof(tmp), nullptr);
    if (r < 0) return false;
    if (static_cast<size_t>(r) != count) return false;
    std::memcpy(buf, tmp, count);
    return true;
}

std::unique_ptr<ConnMq> ConnMq::createHost(SharedSems* sems) {
    return std::make_unique<ConnMq>(sems, true);
}

std::unique_ptr<ConnMq> ConnMq::createClient(SharedSems* sems) {
    return std::make_unique<ConnMq>(sems, false);
}