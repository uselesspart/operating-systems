#define _GNU_SOURCE
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <syslog.h>
#include <unistd.h>

static char saved_ident[128] = "unknown";
static int saved_facility = LOG_USER;

void openlog(const char *ident, int option, int facility) {
    (void)option;
    if (ident != NULL) {
        (void)snprintf(saved_ident, sizeof(saved_ident), "%s", ident);
    }
    saved_facility = facility;
}

void closelog(void) {}

static void capture(int priority, const char *format, va_list args) {
    char message[8192];
    char line[9000];
    const char *path = getenv("DISKMON_TEST_LOG");
    const int encodedFacility = priority & LOG_FACMASK;
    const int facility = encodedFacility != 0 ? encodedFacility : saved_facility;
    const int severity = priority & LOG_PRIMASK;
    int length;
    int descriptor;
    if (path == NULL || *path == '\0') {
        return;
    }
    (void)vsnprintf(message, sizeof(message), format, args);
    length = snprintf(line, sizeof(line), "facility=%d severity=%d ident=%s message=%s\n",
                      facility, severity, saved_ident, message);
    if (length <= 0) {
        return;
    }
    if ((size_t)length >= sizeof(line)) {
        length = (int)sizeof(line) - 1;
    }
    descriptor = (int)syscall(SYS_openat, AT_FDCWD, path,
                              O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (descriptor < 0) {
        return;
    }
    (void)syscall(SYS_write, descriptor, line, (size_t)length);
    (void)syscall(SYS_close, descriptor);
}

void syslog(int priority, const char *format, ...) {
    va_list args;
    va_start(args, format);
    capture(priority, format, args);
    va_end(args);
}

void __syslog_chk(int priority, int flag, const char *format, ...) {
    va_list args;
    (void)flag;
    va_start(args, format);
    capture(priority, format, args);
    va_end(args);
}
