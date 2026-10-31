#pragma once

#include <csignal>
#include <string>

namespace reminder {
// Launches a terminal; reminder text is always passed as a separate argument.
class TerminalNotifier final {
public:
    explicit TerminalNotifier(const std::string& terminal) : terminal_(terminal) {}
    static void reapChildren();
    void show(const std::string& message, const sigset_t& originalMask) const;
private:
    std::string terminal_;
};
}
