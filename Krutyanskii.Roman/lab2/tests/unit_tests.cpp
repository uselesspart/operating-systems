#include <cassert>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <iostream>

#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../src/common.h"
#include "../src/conn_mmap.h"
#include "../src/conn_shm.h"
#include "../src/conn_pipe.h"

static constexpr size_t MSG = 32;

// Wait on a semaphore with a timeout, mirroring the host/kid implementation.
static bool waitMs(sem_t* s, int ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += static_cast<long>(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    while (sem_timedwait(s, &ts) == -1) {
        if (errno == EINTR) continue;
        return false;
    }
    return true;
}

static bool shmNameExists() {
    int fd = shm_open("/lab2_shm", O_RDONLY, 0);
    if (fd >= 0) {
        close(fd);
        return true;
    }
    return false;
}

void test_conn_mmap() {
    ConnMmap c;
    char w[MSG] = "mmap_test";
    assert(c.Write(w, sizeof(w)));
    char r[MSG] = {};
    assert(c.Read(r, sizeof(r)));
    assert(std::strcmp(r, w) == 0);
    std::cout << "PASS: conn_mmap" << std::endl;
}

void test_conn_shm() {
    ConnShm c;
    char w[MSG] = "shm_test";
    assert(c.Write(w, sizeof(w)));
    char r[MSG] = {};
    assert(c.Read(r, sizeof(r)));
    assert(std::strcmp(r, w) == 0);
    std::cout << "PASS: conn_shm" << std::endl;
}

void test_conn_pipe() {
    ConnPipe c;
    char w[MSG] = "pipe_test";
    assert(c.Write(w, sizeof(w)));
    char r[MSG] = {};
    assert(c.Read(r, sizeof(r)));
    assert(std::strcmp(r, w) == 0);
    std::cout << "PASS: conn_pipe" << std::endl;
}

// All interaction types must be usable through the common Conn interface.
void test_conn_polymorphism() {
    ConnMmap m;
    ConnShm s;
    ConnPipe p;
    Conn* conns[] = {&m, &s, &p};
    for (Conn* c : conns) {
        char w[MSG] = "poly";
        assert(c->Write(w, sizeof(w)));
        char r[MSG] = {};
        assert(c->Read(r, sizeof(r)));
        assert(std::strcmp(r, w) == 0);
    }
    std::cout << "PASS: Conn base-pointer dispatch" << std::endl;
}

// The named shared memory object must be removed when ConnShm is destroyed.
void test_conn_shm_cleanup() {
    {
        ConnShm c;
        assert(shmNameExists());
        char w[MSG] = "cleanup";
        assert(c.Write(w, sizeof(w)));
    }
    assert(!shmNameExists());
    std::cout << "PASS: conn_shm cleans up /dev/shm entry" << std::endl;
}

// A stream pipe that delivers fewer bytes than requested must time out and
// report failure instead of blocking forever (guards the fixed-size framing).
void test_conn_pipe_underflow_timeout() {
    ConnPipe c;
    char w[8] = "short";
    assert(c.Write(w, sizeof(w)));
    char r[MSG] = {};
    assert(!c.Read(r, sizeof(r)));
    std::cout << "PASS: conn_pipe underflow times out" << std::endl;
}

void test_semaphore_timeout() {
    sem_t s;
    sem_init(&s, 0, 0);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 0;
    ts.tv_nsec += 200000000;
    int res = sem_timedwait(&s, &ts);
    assert(res == -1);
    sem_destroy(&s);
    std::cout << "PASS: semaphore timeout (sem_timedwait)" << std::endl;
}

// Local semaphores placed in MAP_SHARED memory must synchronize across fork.
void test_local_semaphore_across_fork() {
    struct Shared {
        sem_t sem;
    };
    Shared* sh = static_cast<Shared*>(mmap(nullptr, sizeof(Shared),
                                           PROT_READ | PROT_WRITE,
                                           MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    assert(sh != MAP_FAILED);
    assert(sem_init(&sh->sem, 1, 0) == 0);

    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        sem_post(&sh->sem);
        _exit(0);
    }

    assert(waitMs(&sh->sem, 2000));
    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    sem_destroy(&sh->sem);
    munmap(sh, sizeof(Shared));
    std::cout << "PASS: local semaphore across fork" << std::endl;
}

// Wolf & Kids state rule: a living kid hides at |wolf-kid| <= 70, a dead kid
// revives at |wolf-kid| <= 20 (n = 1).
void test_kid_next_alive() {
    assert(kidNextAlive(true, 50, 50));
    assert(kidNextAlive(true, 1, 71));    // diff 70 -> hides
    assert(!kidNextAlive(true, 1, 72));   // diff 71 -> caught
    assert(!kidNextAlive(true, 100, 29)); // diff 71 -> caught
    assert(kidNextAlive(false, 50, 30));  // diff 20 -> revives
    assert(!kidNextAlive(false, 50, 29)); // diff 21 -> stays dead
    assert(kidNextAlive(false, 50, 70));  // diff 20 -> revives
    assert(!kidNextAlive(false, 50, 71)); // diff 21 -> stays dead
    std::cout << "PASS: kidNextAlive state transitions" << std::endl;
}

int main() {
    test_conn_mmap();
    test_conn_shm();
    test_conn_pipe();
    test_conn_polymorphism();
    test_conn_shm_cleanup();
    test_conn_pipe_underflow_timeout();
    test_semaphore_timeout();
    test_local_semaphore_across_fork();
    test_kid_next_alive();
    std::cout << "=== ALL UNIT TESTS PASSED ===" << std::endl;
    return 0;
}
