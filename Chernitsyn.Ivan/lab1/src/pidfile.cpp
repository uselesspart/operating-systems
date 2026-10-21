#include "pidfile.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

pid_t pidfile_read(const char *path)
{
    FILE *f = fopen(path, "r");
    long pid = 0;

    if (!f)
        return 0;
    if (fscanf(f, "%ld", &pid) != 1 || pid <= 0)
        pid = 0;
    fclose(f);
    return (pid_t)pid;
}

int pidfile_write(const char *path, pid_t pid)
{
    FILE *f = fopen(path, "w");

    if (!f)
        return -1;
    fprintf(f, "%ld\n", (long)pid);
    return fclose(f) == 0 ? 0 : -1;
}

void pidfile_remove(const char *path)
{
    if (pidfile_read(path) == getpid())
        unlink(path);
}

int process_alive(pid_t pid)
{
    char path[64];
    struct stat st;

    if (pid <= 0)
        return 0;

    snprintf(path, sizeof(path), "/proc/%ld", (long)pid);
    return stat(path, &st) == 0;
}

int stop_running(const char *path)
{
    pid_t pid = pidfile_read(path);
    struct timespec ts = {0, 100 * 1000 * 1000};
    int i;

    if (pid <= 0 || pid == getpid() || !process_alive(pid))
        return 0;

    if (kill(pid, SIGTERM) != 0)
        return errno == ESRCH ? 0 : -1;

    for (i = 0; i < 50; i++) {
        if (!process_alive(pid))
            return 1;
        nanosleep(&ts, NULL);
    }

    syslog(LOG_WARNING, "old instance %ld did not stop in time", (long)pid);
    return -1;
}
