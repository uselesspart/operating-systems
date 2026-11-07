#include <fcntl.h>
#include <mqueue.h>
#include <unistd.h>

#include <cerrno>
#include <string>

#include "connection.h"

namespace {

constexpr mqd_t kNoQueue = -1;

void closeQueue(mqd_t& queue)
{
    if (queue != kNoQueue) {
        mq_close(queue);
        queue = kNoQueue;
    }
}

class MqConnection : public Connection {
public:
    MqConnection()
        : m_h2c("/os_lab2_" + std::to_string(getpid()) + "_h2c"),
          m_c2h("/os_lab2_" + std::to_string(getpid()) + "_c2h")
    {
    }

    ~MqConnection() override
    {
        closeQueue(m_in);
        closeQueue(m_out);
        if (isOwner()) {
            mq_unlink(m_h2c.c_str());
            mq_unlink(m_c2h.c_str());
        }
    }

    int readFd() const override { return m_in; }

private:
    bool hostCreate() override
    {
        mq_attr attr{};
        attr.mq_maxmsg = kSlots;
        attr.mq_msgsize = sizeof(WireMessage);
        return createQueue(m_out, m_h2c, O_WRONLY, attr) && createQueue(m_in, m_c2h, O_RDONLY, attr);
    }

    bool clientOpen() override
    {
        return openQueue(m_in, m_h2c, O_RDONLY) && openQueue(m_out, m_c2h, O_WRONLY);
    }

    bool createQueue(mqd_t& queue, const std::string& name, int access, mq_attr& attr)
    {
        queue = mq_open(name.c_str(), O_CREAT | O_EXCL | access | O_NONBLOCK, 0600, &attr);
        if (queue != kNoQueue)
            return true;
        setError(errnoMessage("не удалось создать очередь " + name
                              + " (mq_open; проверьте /proc/sys/fs/mqueue/msg_max и msgsize_max)"));
        return false;
    }

    bool openQueue(mqd_t& queue, const std::string& name, int access)
    {
        queue = mq_open(name.c_str(), access | O_NONBLOCK);
        if (queue != kNoQueue)
            return true;
        setError(errnoMessage("не удалось открыть очередь " + name));
        return false;
    }

    SendResult writeFrame(const WireMessage& message) override
    {
        if (mq_send(m_out, reinterpret_cast<const char*>(&message), sizeof message, 0) == 0)
            return SendResult::Ok;
        if (errno == EAGAIN)
            return SendResult::Busy;
        setError(errnoMessage("ошибка отправки (mq_send)"));
        return SendResult::Error;
    }

    RecvResult readFrame(WireMessage& message) override
    {
        const ssize_t n = mq_receive(m_in, reinterpret_cast<char*>(&message), sizeof message, nullptr);
        if (n == static_cast<ssize_t>(sizeof message))
            return RecvResult::Message;
        if (n >= 0) {
            setError("получен кадр неверного размера");
            return RecvResult::Error;
        }
        if (errno == EAGAIN)
            return RecvResult::Empty;
        setError(errnoMessage("ошибка приёма (mq_receive)"));
        return RecvResult::Error;
    }

    std::string m_h2c;
    std::string m_c2h;
    mqd_t m_in = kNoQueue;
    mqd_t m_out = kNoQueue;
};

}

std::unique_ptr<Connection> createConnection()
{
    return std::make_unique<MqConnection>();
}
