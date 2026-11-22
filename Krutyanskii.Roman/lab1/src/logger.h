#ifndef LAB1_LOGGER_H
#define LAB1_LOGGER_H

namespace lab1 {
namespace log {

/// Connects the process to the system journal under the given identity.
void open(const char* ident);

/// Writes an informational message to the system journal.
void info(const char* format, ...) __attribute__((format(printf, 1, 2)));

/// Writes an error message to the system journal.
void error(const char* format, ...) __attribute__((format(printf, 1, 2)));

/// Disconnects the process from the system journal.
void close();

}  // namespace log
}  // namespace lab1

#endif  // LAB1_LOGGER_H
