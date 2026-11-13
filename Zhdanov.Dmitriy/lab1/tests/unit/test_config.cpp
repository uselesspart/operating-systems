#include "Config.hpp"
#include "check.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

static void writeText(const std::string& path, const std::string& text) {
    std::ofstream output(path, std::ios::trunc);
    output << text;
}

int main() {
    std::string path =
        "/tmp/reminder_config_test_" + std::to_string(static_cast<long long>(getpid())) + ".conf";

    writeText(path, "# комментарий\n\n1700000000 none Первое событие\n1700003600 day Второе событие\n");
    std::vector<Event> events;
    std::string error;
    CHECK(loadEvents(path, events, error));
    CHECK_EQ(events.size(), static_cast<std::size_t>(2));
    CHECK_EQ(events[0].timestamp, 1700000000LL);
    CHECK(events[0].repeat == Repeat::None);
    CHECK_EQ(events[0].text, std::string("Первое событие"));
    CHECK_EQ(events[1].text, std::string("Второе событие"));

    writeText(path, "1700000000 month Неверный флаг\n");
    CHECK(!loadEvents(path, events, error));
    CHECK(!error.empty());

    writeText(path, "1700000000 none\n");
    CHECK(!loadEvents(path, events, error));

    CHECK(!loadEvents(path + ".missing", events, error));

    std::vector<Event> saved{Event{1700000000LL, Repeat::Hour, "Текст с пробелами"},
                             Event{1700003600LL, Repeat::None, "Ещё одно"}};
    CHECK(saveEvents(path, saved, error));
    events.clear();
    CHECK(loadEvents(path, events, error));
    CHECK_EQ(events.size(), static_cast<std::size_t>(2));
    CHECK_EQ(events[0].text, std::string("Текст с пробелами"));
    CHECK(events[0].repeat == Repeat::Hour);

    std::vector<Event> empty;
    CHECK(saveEvents(path, empty, error));
    CHECK(loadEvents(path, events, error));
    CHECK_EQ(events.size(), static_cast<std::size_t>(0));

    std::remove(path.c_str());
    return finishTests();
}
