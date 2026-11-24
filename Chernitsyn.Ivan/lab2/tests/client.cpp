#include CONN_HEADER

#include <stdint.h>
#include <criterion/criterion.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static pid_t start_client(Session *session, Conn *conn)
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    cr_assert_eq(sigprocmask(SIG_BLOCK, &mask, nullptr), 0);
    cr_assert(session->open(getpid(), true), "%s", strerror(errno));
    cr_assert(conn->open(*session, true), "%s", strerror(errno));

    char pid[32];
    snprintf(pid, sizeof(pid), "%d", getpid());
    pid_t child = fork();
    cr_assert_neq(child, -1);
    if (child == 0) {
        execl(CLIENT_PATH, CLIENT_PATH, pid, (char *)nullptr);
        _exit(127);
    }

    timespec timeout = {5, 0};
    siginfo_t info;
    cr_assert_eq(sigtimedwait(&mask, &info, &timeout), SIGUSR1);
    cr_assert_eq(info.si_pid, child);
    return child;
}

static void check_exit(pid_t child, int code)
{
    int status;
    cr_assert_eq(waitpid(child, &status, 0), child);
    cr_assert(WIFEXITED(status));
    cr_assert_eq(WEXITSTATUS(status), code);
}

Test(client, handshake_and_rounds, .timeout = 10)
{
    Session session;
    Conn conn;
    pid_t child = start_client(&session, &conn);
    int replies[] = {0, 1, -1};
    int maximum = 100;
    for (int reply : replies) {
        int value;
        cr_assert(wait_five_seconds(session.request));
        cr_assert(conn.read(&value));
        cr_assert(value >= 1 && value <= maximum);
        cr_assert(conn.write(reply));
        cr_assert_eq(sem_post(session.reply), 0);
        maximum = reply ? 100 : 50;
    }
    check_exit(child, 0);
}

Test(client, silent_host, .timeout = 10)
{
    Session session;
    Conn conn;
    timespec start, stop;
    clock_gettime(CLOCK_MONOTONIC, &start);
    pid_t child = start_client(&session, &conn);
    cr_assert(wait_five_seconds(session.request));
    check_exit(child, 1);
    clock_gettime(CLOCK_MONOTONIC, &stop);
    double seconds = stop.tv_sec - start.tv_sec + (stop.tv_nsec - start.tv_nsec) / 1e9;
    cr_assert_geq(seconds, 5.0);
    cr_assert_eq(sem_trywait(session.reply), -1);
    cr_assert_eq(errno, EAGAIN);
}
