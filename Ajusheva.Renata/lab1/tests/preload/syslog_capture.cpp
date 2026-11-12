
#ifdef _FORTIFY_SOURCE
#undef _FORTIFY_SOURCE
#endif

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <syslog.h>
#include <unistd.h>

namespace {

char g_ident[64] = "unknown";
int g_options = 0;
int g_facility = LOG_USER;

const char* facilityName(int facility) {
    switch (facility) {
        case LOG_KERN:
            return "kern";
        case LOG_USER:
            return "user";
        case LOG_DAEMON:
            return "daemon";
        case LOG_AUTH:
            return "auth";
        case LOG_SYSLOG:
            return "syslog";
        case LOG_LOCAL0:
            return "local0";
        case LOG_LOCAL1:
            return "local1";
        case LOG_LOCAL7:
            return "local7";
        default:
            return "other";
    }
}

const char* severityName(int severity) {
    static const char* const names[] = {"emerg",   "alert",  "crit", "err",
                                        "warning", "notice", "info", "debug"};
    return names[severity & LOG_PRIMASK];
}

void capture(int priority, const char* format, va_list arguments) {
    char message[4096];
    std::vsnprintf(message, sizeof message, format, arguments);

    const int facility = (priority & LOG_FACMASK) != 0 ? (priority & LOG_FACMASK) : g_facility;
    char line[4352];
    const int length = std::snprintf(line, sizeof line, "%s.%s %s[%d]: %s\n", facilityName(facility),
                                     severityName(priority), g_ident, static_cast<int>(::getpid()), message);

    const char* file = std::getenv("DISKMOND_SYSLOG_CAPTURE");
    if (file != nullptr && *file != '\0' && length > 0) {
        
        const int fd = ::open(file, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
        if (fd >= 0) {
            const ssize_t written = ::write(fd, line, static_cast<size_t>(length));
            (void)written;
            ::close(fd);
        }
    }
    if (g_options & LOG_PERROR) {
        ::dprintf(STDERR_FILENO, "%s[%d]: %s\n", g_ident, static_cast<int>(::getpid()), message);
    }
}

}  

extern "C" {

void openlog(const char* ident, int options, int facility) {
    std::snprintf(g_ident, sizeof g_ident, "%s", ident != nullptr ? ident : "unknown");
    g_options = options;
    g_facility = facility;
}

void closelog(void) {}

void vsyslog(int priority, const char* format, va_list arguments) {
    capture(priority, format, arguments);
}

void syslog(int priority, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    capture(priority, format, arguments);
    va_end(arguments);
}


void __vsyslog_chk(int priority, int, const char* format, va_list arguments) {
    capture(priority, format, arguments);
}

void __syslog_chk(int priority, int, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    capture(priority, format, arguments);
    va_end(arguments);
}

}  
