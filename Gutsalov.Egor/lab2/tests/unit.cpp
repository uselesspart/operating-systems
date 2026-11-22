#include "chat.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
int checks = 0;
void check(bool value, const char* name) {
    ++checks;
    if (!value) throw std::runtime_error(name);
}
void rejects(const std::function<void()>& function, const char* name) {
    bool rejected = false;
    try { function(); } catch (const std::exception&) { rejected = true; }
    check(rejected, name);
}
}
int main() {
    try {
        chat::Message original{42, chat::now_ns(), 123, 456, "Привет! \t <b>literal</b>"};
        const auto restored = chat::decode(chat::encode(original));
        check(restored.sequence == 42 && restored.sender == 123 && restored.recipient == 456 &&
              restored.timestamp == original.timestamp && restored.text == original.text, "round trip");
        check(chat::visible_to(original, 123), "private sender");
        check(chat::visible_to(original, 456), "private recipient");
        check(!chat::visible_to(original, 0) && !chat::visible_to(original, 789), "private isolation");
        original.recipient = -1;
        check(chat::visible_to(original, 0) && chat::visible_to(original, 789), "broadcast");
        original.text = std::string(chat::max_text, 'a');
        check(chat::decode(chat::encode(original)).text.size() == chat::max_text, "max payload");
        for (const auto& text : {std::string{}, std::string(chat::max_text + 1, 'a'),
                                std::string("one\ntwo"), std::string("a\0b", 3), std::string("\r")}) {
            original.text = text;
            rejects([&] { chat::encode(original); }, "bad text");
        }
        for (const auto* bytes : {"garbage", "0 x 1 2\ntext", "0 1 -2 3\ntext", "0 1 2 -9\ntext",
                                 "0 1 2 3 extra\ntext", "0 0 1 2\ntext", "0 1 1 2\n"}) {
            rejects([&] { chat::decode(bytes); }, "bad header");
        }
        chat::Events events;
        std::vector<std::thread> threads;
        for (int i = 0; i < 8; ++i) threads.emplace_back([&] {
            for (int j = 0; j < 100; ++j) events.push("event");
        });
        for (auto& thread : threads) thread.join();
        check(events.take().size() == 800, "concurrent event queue");
        check(events.take().empty(), "event drain");
        char program[] = "client", help[] = "--help", bad[] = "-1", pid[] = "12345", headless[] = "--headless";
        char* args[] = {program, pid, headless};
        check(chat::options(3, args, true).host == 12345 && chat::options(3, args, true).headless, "client options");
        char* help_args[] = {program, help};
        check(chat::options(2, help_args, true).help, "help without PID");
        char* bad_args[] = {program, bad};
        rejects([&] { chat::options(2, bad_args, true); }, "invalid PID");
        rejects([&] { chat::options(1, args, true); }, "missing PID");
        std::cout << checks << " unit checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
