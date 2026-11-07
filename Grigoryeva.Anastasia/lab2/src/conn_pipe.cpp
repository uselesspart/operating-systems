// Тип взаимодействия "pipe" - программные (анонимные) каналы.
//
// Анонимный канал нельзя передать НЕЗАВИСИМОМУ процессу через наследование, поэтому:
//  * хост создаёт две пары pipe() на каждого клиента и переносит их дескрипторы (dup2) на детерминированные номера
//    kFdBase + 4*id + {0..3};
//  * клиент (знает pid хоста и свой id) открывает нужные концы через /proc/<pid хоста>/fd/<номер>
//    - так получается доступ к тому же самому каналу.
// Порядок операций синхронизируют глобальные семафоры (см. sync_sem.h).
#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

#include "conn.h"
#include "fd_io.h"
#include "sync_sem.h"

namespace {

constexpr int kFdBase = 1000;
constexpr int kFdsPerKid = 4;
// A - канал хост -> клиент, B - канал клиент -> хост
enum { kReadA = 0, kWriteA = 1, kReadB = 2, kWriteB = 3 };

std::string ProcFd(pid_t pid, int fd) {
    return "/proc/" + std::to_string(static_cast<long>(pid)) + "/fd/" + std::to_string(fd);
}

void EnsureFdLimit(int needed) {
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) != 0 || rl.rlim_cur >= static_cast<rlim_t>(needed)) return;
    rl.rlim_cur = (rl.rlim_max != RLIM_INFINITY && rl.rlim_max < static_cast<rlim_t>(needed))
                      ? rl.rlim_max
                      : static_cast<rlim_t>(needed);
    setrlimit(RLIMIT_NOFILE, &rl);
}

// Переносит дескриптор src на номер target (который должен быть свободен).
void PlaceAt(UniqueFd& dst, const UniqueFd& src, int target) {
    if (fcntl(target, F_GETFD) != -1 || errno != EBADF) {
        throw std::runtime_error("дескриптор " + std::to_string(target) + " уже занят");
    }
    if (dup2(src.get(), target) < 0) {
        throw std::runtime_error("dup2 -> " + std::to_string(target) + ": " + std::strerror(errno));
    }
    fcntl(target, F_SETFD, FD_CLOEXEC);
    dst.reset(target);
}

}  // namespace

struct Conn::Impl {
    SemChannel sem;
    bool host;
    // хост: все 4 дескриптора; клиент: только kReadA и kWriteB
    UniqueFd fds[kFdsPerKid];
    std::string err;

    Impl(const ConnId& id, bool create) : sem(id, create), host(create) {
        const int base = kFdBase + id.id * kFdsPerKid;
        if (create) {
            EnsureFdLimit(base + kFdsPerKid + 16);
            int a[2], b[2];
            if (pipe(a) != 0) throw std::runtime_error(std::string("pipe(): ") + std::strerror(errno));
            UniqueFd a0(a[0]), a1(a[1]);
            if (pipe(b) != 0) throw std::runtime_error(std::string("pipe(): ") + std::strerror(errno));
            UniqueFd b0(b[0]), b1(b[1]);
            PlaceAt(fds[kReadA], a0, base + kReadA);
            PlaceAt(fds[kWriteA], a1, base + kWriteA);
            PlaceAt(fds[kReadB], b0, base + kReadB);
            PlaceAt(fds[kWriteB], b1, base + kWriteB);
        } else {
            const std::string pathR = ProcFd(id.hostPid, base + kReadA);
            const std::string pathW = ProcFd(id.hostPid, base + kWriteB);
            fds[kReadA].reset(open(pathR.c_str(), O_RDONLY | O_CLOEXEC));
            if (!fds[kReadA].valid()) throw std::runtime_error("open " + pathR + ": " + std::strerror(errno));
            fds[kWriteB].reset(open(pathW.c_str(), O_WRONLY | O_CLOEXEC));
            if (!fds[kWriteB].valid()) throw std::runtime_error("open " + pathW + ": " + std::strerror(errno));
        }
    }
};

Conn::Conn(const ConnId& id, bool create) : impl_(new Impl(id, create)) {}
Conn::~Conn() = default;

const std::string& Conn::LastError() const { return impl_->err; }

bool Conn::Read(void* buf, size_t count) {
    Impl& d = *impl_;
    if (!d.sem.WaitIncoming(IoTimeoutMs(), d.err)) return false;
    return ReadFdExact(d.fds[d.host ? kReadB : kReadA].get(), buf, count, IoTimeoutMs(), d.err);
}

bool Conn::Write(const void* buf, size_t count) {
    Impl& d = *impl_;
    if (!WriteFdAll(d.fds[d.host ? kWriteA : kWriteB].get(), buf, count, IoTimeoutMs(), false, d.err)) return false;
    d.sem.PostOutgoing();
    return true;
}
