// Capture calls to the real syslog API without replacing the host's /dev/log.
// Linked only into the test driver; not linked into the production daemon.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <syslog.h>

namespace {

void record(const char *kind, int priority, const char *message) {
  const char *path = std::getenv("LAB1_TEST_SYSLOG");
  if (path == nullptr) {
    return;
  }
  if (FILE *file = std::fopen(path, "a")) {
    std::fprintf(file, "%s %d %s\n", kind, priority, message);
    std::fclose(file);
  }
}

void recordFormatted(int priority, const char *format, va_list args) {
  char message[4096];
  std::vsnprintf(message, sizeof(message), format, args);
  record("syslog", priority, message);
}

} // namespace

extern "C" void openlog(const char *ident, int, int facility) {
  record("openlog", facility, ident == nullptr ? "" : ident);
}

extern "C" void syslog(int priority, const char *format, ...) {
  va_list args;
  va_start(args, format);
  recordFormatted(priority, format, args);
  va_end(args);
}

// Optimized glibc builds with _FORTIFY_SOURCE redirect syslog to this wrapper.
extern "C" void __syslog_chk(int priority, int, const char *format, ...) {
  va_list args;
  va_start(args, format);
  recordFormatted(priority, format, args);
  va_end(args);
}

extern "C" void closelog() {
  record("closelog", 0, "");
}
