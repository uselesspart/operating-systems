#include "Event.hpp"
#include "check.hpp"

int main() {
    Event event;

    CHECK(parseEvent("1700000000 none Позвонить маме", event));
    CHECK_EQ(event.timestamp, 1700000000LL);
    CHECK(event.repeat == Repeat::None);
    CHECK_EQ(event.text, std::string("Позвонить маме"));

    CHECK(parseEvent("1700000000 hour Выпить воды", event));
    CHECK(event.repeat == Repeat::Hour);
    CHECK_EQ(event.text, std::string("Выпить воды"));

    CHECK(parseEvent("1700000000 day Проверить почту", event));
    CHECK(event.repeat == Repeat::Day);

    CHECK(parseEvent("1700000000 week Подвести итоги", event));
    CHECK(event.repeat == Repeat::Week);

    CHECK(parseEvent("  1700000000   week   Текст с пробелами  ", event));
    CHECK_EQ(event.text, std::string("Текст с пробелами  "));

    CHECK(!parseEvent("", event));
    CHECK(!parseEvent("1700000000 none", event));
    CHECK(!parseEvent("1700000000 none   ", event));
    CHECK(!parseEvent("0 none Текст", event));
    CHECK(!parseEvent("-5 hour Текст", event));
    CHECK(!parseEvent("текст none Текст", event));
    CHECK(!parseEvent("1700000000 month Текст", event));
    CHECK(!parseEvent("1700000000", event));

    Event formatted{1700000000LL, Repeat::Week, "Сдать отчёт"};
    CHECK_EQ(formatEvent(formatted), std::string("1700000000 week Сдать отчёт"));

    CHECK_EQ(repeatPeriod(Repeat::None), 0LL);
    CHECK_EQ(repeatPeriod(Repeat::Hour), 3600LL);
    CHECK_EQ(repeatPeriod(Repeat::Day), 86400LL);
    CHECK_EQ(repeatPeriod(Repeat::Week), 604800LL);

    Event hourly{1000LL, Repeat::Hour, "Текст"};
    moveToFuture(hourly, 1000);
    CHECK_EQ(hourly.timestamp, 4600LL);

    hourly.timestamp = 1000;
    moveToFuture(hourly, 1000 + 3 * 3600 + 5);
    CHECK_EQ(hourly.timestamp, 1000 + 4 * 3600);

    Event once{1000LL, Repeat::None, "Текст"};
    moveToFuture(once, 5000);
    CHECK_EQ(once.timestamp, 1000LL);

    return finishTests();
}
