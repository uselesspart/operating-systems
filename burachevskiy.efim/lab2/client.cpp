#include "session.hpp"

#include <cstdio>
#include <deque>

namespace {
class Console {
    std::string partial_;
    std::deque<std::string> lines_;
    bool eof_ = false;
    bool tooLong_ = false;

public:
    Frame next() {
        char buffer[4096];
        pollfd input{STDIN_FILENO, POLLIN, 0};
        const int ready = poll(&input, 1, 0);
        if (ready < 0 && errno != EINTR) fail("poll stdin");
        if (!eof_ && lines_.empty() && ready > 0 && (input.revents & (POLLIN | POLLHUP))) {
            const auto count = read(STDIN_FILENO, buffer, sizeof(buffer));
            if (count < 0 && errno != EAGAIN && errno != EINTR) fail("read stdin");
            if (count == 0) {
                eof_ = true;
                if (!partial_.empty() && !tooLong_) lines_.push_back(partial_);
            }
            for (ssize_t i = 0; i < count; ++i) {
                if (buffer[i] == '\n') {
                    if (tooLong_) std::fputs("Слишком длинное сообщение\n", stderr);
                    else if (!partial_.empty()) lines_.push_back(partial_);
                    partial_.clear();
                    tooLong_ = false;
                } else if (partial_.size() < frameSize - 2) {
                    partial_.push_back(buffer[i]);
                } else {
                    tooLong_ = true;
                }
            }
        }
        while (!lines_.empty()) {
            auto line = std::move(lines_.front());
            lines_.pop_front();
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line == "/quit") return frame('Q');
            if (line.rfind("/host ", 0) == 0) {
                if (line.size() > 6) return frame('P', line.substr(6));
            } else if (!line.empty()) {
                return frame('A', line);
            }
        }
        return frame('N');
    }
};
}

int runClient(Shared& shared, const std::string& directory, int hostSocket, int clientSocket) {
    try {
        wait(shared.started);
        Conn connection(false, directory, hostSocket, clientSocket);
        Console console;
        bool welcomed = false;
        std::puts("Клиент подключён. Текст — всем, /host текст — лично хосту, /quit — выход.");
        std::fflush(stdout);
        for (;;) {
            wait(shared.request);
            Frame request{};
            connection.Read(request.data(), request.size());
            if (request[0] == 'Q') {
                const auto reply = frame('Q');
                connection.Write(reply.data(), reply.size());
                post(shared.reply);
                return 0;
            }
            if (request[0] == 'D') {
                std::puts(message(request).c_str());
                std::fflush(stdout);
            }
            const auto reply = welcomed ? console.next() : frame('C');
            welcomed = true;
            connection.Write(reply.data(), reply.size());
            post(shared.reply);
            if (reply[0] == 'Q') return 0;
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Клиент: %s\n", error.what());
        return 1;
    }
}
