#include "connection.h"

#include <semaphore.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <ctime>
#include <stdexcept>

namespace {

enum Sem { HostReady, ClientReady, LinkReady, ToClient, ToHost, SemCount };

timespec deadlineIn(int timeoutMs)
{
    timespec deadline{};
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeoutMs / 1000;
    deadline.tv_nsec += static_cast<long>(timeoutMs % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }
    return deadline;
}

}

struct SharedSync {
    sem_t sems[SemCount];
};

Connection::Connection() : m_ownerPid(getpid())
{
    void* memory = mmap(nullptr, sizeof(SharedSync), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED)
        throw std::runtime_error(errnoMessage("не удалось создать общую память (mmap)"));
    m_sync = static_cast<SharedSync*>(memory);

    for (int i = 0; i < SemCount; ++i) {
        const unsigned initial = i >= ToClient ? kSlots : 0;
        if (sem_init(&m_sync->sems[i], 1, initial) != 0) {
            const std::string text = errnoMessage("не удалось создать семафор (sem_init)");
            for (int created = 0; created < i; ++created)
                sem_destroy(&m_sync->sems[created]);
            munmap(m_sync, sizeof(SharedSync));
            throw std::runtime_error(text);
        }
    }
}

Connection::~Connection()
{
    if (isOwner()) {
        for (int i = 0; i < SemCount; ++i)
            sem_destroy(&m_sync->sems[i]);
    }
    munmap(m_sync, sizeof(SharedSync));
}

bool Connection::isOwner() const
{
    return getpid() == m_ownerPid;
}

std::string Connection::errnoMessage(const std::string& what)
{
    return what + ": " + std::strerror(errno);
}

bool Connection::connect(Party self, int timeoutMs)
{
    if (self == Party::Everyone) {
        setError("неверная роль участника");
        return false;
    }
    m_self = self;

    auto post = [this](Sem id) {
        if (sem_post(&m_sync->sems[id]) == 0)
            return true;
        setError(errnoMessage("sem_post"));
        return false;
    };
    auto wait = [this, timeoutMs](Sem id) {
        const timespec deadline = deadlineIn(timeoutMs);
        while (sem_timedwait(&m_sync->sems[id], &deadline) != 0) {
            if (errno == EINTR)
                continue;
            setError(errno == ETIMEDOUT
                         ? "тайм-аут синхронизации: собеседник не ответил за " + std::to_string(timeoutMs) + " мс"
                         : errnoMessage("sem_timedwait"));
            return false;
        }
        return true;
    };

    if (self == Party::Host)
        m_connected = hostCreate() && post(HostReady) && wait(ClientReady) && hostFinish() && post(LinkReady);
    else
        m_connected = wait(HostReady) && clientOpen() && post(ClientReady) && wait(LinkReady);
    return m_connected;
}

SendResult Connection::send(const WireMessage& message)
{
    if (!m_connected) {
        setError("соединение не установлено");
        return SendResult::Error;
    }
    sem_t* slots = &m_sync->sems[m_self == Party::Host ? ToClient : ToHost];
    if (sem_trywait(slots) != 0) {
        if (errno == EAGAIN)
            return SendResult::Busy;
        setError(errnoMessage("sem_trywait"));
        return SendResult::Error;
    }
    const SendResult result = writeFrame(message);
    if (result != SendResult::Ok && sem_post(slots) != 0) {
        const std::string cause = errnoMessage("не удалось вернуть слот (sem_post)");
        setError(result == SendResult::Error ? m_error + "; " + cause : cause);
        return SendResult::Error;
    }
    return result;
}

RecvResult Connection::receive(WireMessage& message)
{
    if (!m_connected) {
        setError("соединение не установлено");
        return RecvResult::Error;
    }
    const RecvResult result = readFrame(message);
    if (result != RecvResult::Message)
        return result;
    if (sem_post(&m_sync->sems[m_self == Party::Host ? ToHost : ToClient]) != 0) {
        setError(errnoMessage("не удалось вернуть слот (sem_post)"));
        return RecvResult::Error;
    }
    if (!isValid(message)) {
        setError("получен повреждённый кадр");
        return RecvResult::Error;
    }
    return RecvResult::Message;
}
