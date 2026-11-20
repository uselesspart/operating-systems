#include "Check.hpp"
#include "Message.hpp"
#include "MessageOrder.hpp"
#include "SharedSession.hpp"
#include "ChildProcess.hpp"

#include <csignal>
#include <sys/wait.h>

using namespace chat;

int main() {
    try {
        const auto original = Message::compose(Party::Client, Party::Everyone, 1, 100, "Привет <b> & $(echo test)");
        expect(original.body() == "Привет <b> & $(echo test)", "UTF-8 and literal text");
        expect(original.visibleTo(Party::Host) && original.visibleTo(Party::Client), "Public routing");
        expect(!original.visibleTo(Party::Everyone), "Everyone is not a participant");
        auto personal = Message::compose(Party::Host, Party::Client, 2, 200, "private");
        expect(personal.visibleTo(Party::Host) && personal.visibleTo(Party::Client), "Private visible to sender and recipient");
        auto selfOnly = Message::compose(Party::Host, Party::Host, 1, 100, "self");
        expect(!selfOnly.visibleTo(Party::Client), "Private not visible to third party");
        rejects([] { Message::compose(Party::Host, Party::Everyone, 1, 1, ""); }, "Reject empty text");
        rejects([] { Message::compose(Party::Host, Party::Everyone, 1, 1, std::string(769, 'x')); }, "Reject oversized text");
        rejects([] { Message::compose(Party::Everyone, Party::Host, 1, 1, "bad"); }, "Reject invalid sender");
        rejects([] { Message::compose(Party::Host, Party::Everyone, 0, 1, "bad"); }, "Reject zero sequence");
        rejects([] { Message::compose(Party::Host, Party::Everyone, 1, 1, std::string("a\0b", 3)); }, "Reject embedded NUL");
        auto corrupt = original;
        corrupt.length = 900;
        rejects([&] { corrupt.validate(); }, "Reject corrupted length");
        corrupt = original;
        corrupt.magic = 0;
        rejects([&] { corrupt.validate(); }, "Reject packet magic");
        corrupt = original;
        corrupt.kind = static_cast<PacketKind>(8);
        rejects([&] { corrupt.validate(); }, "Reject packet kind");
        MessageOrder order;
        order.accept(personal);
        expect(order.takeReady().empty() && order.hasGap(), "Wait for delayed earlier request");
        order.accept(original);
        const auto ready = order.takeReady();
        expect(ready.size() == 2 && ready[0].sequence == 1 && ready[1].sequence == 2, "Reorder by submission time");
        expect(ready[0].kind == PacketKind::Delivery, "Host converts requests to deliveries");
        rejects([&] { order.accept(original); }, "Reject repeated delivery");
        order.accept(Message::compose(Party::Host, Party::Everyone, 3, 150, "old"));
        rejects([&] { order.takeReady(); }, "Reject backwards timestamp");
        SharedSession shared;
        shared.startActivityClock();
        const auto first = shared.reserve(Party::Host);
        expect(first.sequence == 1, "First global sequence");
        const auto child = fork();
        if (child < 0) throw std::runtime_error("fork failed");
        if (child == 0) {
            try { const auto second = shared.reserve(Party::Client); _exit(second.sequence == 2 ? 0 : 1); }
            catch (...) { _exit(1); }
        }
        ChildProcess guard(child);
        int status = 0;
        eventually([&] { return waitpid(child, &status, WNOHANG) == child; }, "Child reserved global sequence");
        expect(WIFEXITED(status) && WEXITSTATUS(status) == 0, "Shared semaphore works between processes");
        const auto third = shared.reserve(Party::Host);
        expect(third.sequence == 3 && third.sentAtNs > first.sentAtNs, "Global order after fork");
        const auto now = SharedSession::monotonicNs();
        expect(!shared.clientIdle(now + 59000000000LL), "No premature inactivity termination");
        expect(shared.clientIdle(now + 61000000000LL), "Idle after more than a minute");
        shared.reserve(Party::Client);
        expect(!shared.clientIdle(SharedSession::monotonicNs()), "Client submission resets idle clock");
        shared.wait(SharedSession::Gate::HostEmpty);
        expect(!shared.tryWait(SharedSession::Gate::HostEmpty), "Semaphore credit cannot be consumed twice");
        shared.post(SharedSession::Gate::HostEmpty);
        expect(shared.tryWait(SharedSession::Gate::HostEmpty), "Semaphore credit returned");
        const auto previousHandler = std::signal(SIGUSR1, +[](int) {});
        const auto parent = getpid();
        const auto interrupter = fork();
        if (interrupter < 0) throw std::runtime_error("fork timeout test");
        if (interrupter == 0) {
            for (int i = 0; i < 10; ++i) { usleep(100000); kill(parent, SIGUSR1); }
            _exit(0);
        }
        ChildProcess interruptGuard(interrupter);
        const auto waitStart = SharedSession::monotonicNs();
        rejects([&] { shared.wait(SharedSession::Gate::Created); }, "Semaphore times out even with EINTR");
        const auto elapsed = SharedSession::monotonicNs() - waitStart;
        expect(elapsed >= 4900000000LL && elapsed < 6500000000LL, "Signals do not reset the 5-second deadline");
        std::signal(SIGUSR1, previousHandler);
        std::cout << "Model: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Model FAILED: " << error.what() << '\n';
        return 1;
    }
}
