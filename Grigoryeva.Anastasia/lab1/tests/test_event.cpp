// Модульные тесты разбора конфигурации и расчёта времени срабатывания (без внешних библиотек).
#include <cstring>
#include <ctime>
#include <iostream>
#include <sstream>

#include "Event.h"

static int g_total = 0, g_failed = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++g_total;                                                                    \
        if (!(cond)) {                                                                \
            ++g_failed;                                                               \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #cond "\n";   \
        }                                                                             \
    } while (0)

static std::time_t at(int y, int m, int d, int h, int mi, int s = 0) {
    std::tm t;
    std::memset(&t, 0, sizeof(t));
    t.tm_year = y - 1900;
    t.tm_mon = m - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    t.tm_min = mi;
    t.tm_sec = s;
    t.tm_isdst = -1;
    return std::mktime(&t);
}

static void testParseValid() {
    Event e;
    std::string err;
    CHECK(parseEventLine("2030-05-17 09:30 Позвонить маме", e, err));
    CHECK(e.start == at(2030, 5, 17, 9, 30));
    CHECK(e.repeat == Repeat::None);
    CHECK(e.text == "Позвонить маме");

    CHECK(parseEventLine("2030-05-17 09:30:15 -h Каждый час", e, err));
    CHECK(e.start == at(2030, 5, 17, 9, 30, 15));
    CHECK(e.repeat == Repeat::Hourly);
    CHECK(e.text == "Каждый час");

    CHECK(parseEventLine("2030-05-17 09:30 -d Каждый день", e, err));
    CHECK(e.repeat == Repeat::Daily);

    CHECK(parseEventLine("2030-05-17 09:30 -w Каждую неделю", e, err));
    CHECK(e.repeat == Repeat::Weekly);
    CHECK(e.text == "Каждую неделю");

    // лишние пробелы
    CHECK(parseEventLine("  2030-05-17    09:30   -d    много   пробелов  ", e, err));
    CHECK(e.repeat == Repeat::Daily);
    CHECK(e.text == "много   пробелов");
}

static void testFlagLikeText() {
    Event e;
    std::string err;
    // не флаг - значит часть текста
    CHECK(parseEventLine("2030-05-17 09:30 -x текст", e, err));
    CHECK(e.repeat == Repeat::None);
    CHECK(e.text == "-x текст");
    CHECK(parseEventLine("2030-05-17 09:30 -dd что-то", e, err));
    CHECK(e.repeat == Repeat::None);
    CHECK(e.text == "-dd что-то");
    // спецсимволы остаются в тексте как есть
    CHECK(parseEventLine("2030-05-17 09:30 it's \"quoted\" $HOME `ls`", e, err));
    CHECK(e.text == "it's \"quoted\" $HOME `ls`");
}

static void testParseInvalid() {
    Event e;
    std::string err;
    CHECK(!parseEventLine("", e, err));
    CHECK(!parseEventLine("2030-05-17", e, err));
    CHECK(!parseEventLine("2030-05-17 09:30", e, err));        // нет текста
    CHECK(!parseEventLine("2030-05-17 09:30 -d", e, err));     // флаг, но нет текста
    CHECK(!parseEventLine("2030-05-17 09:30 -d   ", e, err));
    CHECK(!parseEventLine("17.05.2030 09:30 текст", e, err));  // неверный формат даты
    CHECK(!parseEventLine("2030-13-01 09:30 текст", e, err));  // месяц 13
    CHECK(!parseEventLine("2030-02-30 09:30 текст", e, err));  // 30 февраля
    CHECK(!parseEventLine("2030-05-17 25:00 текст", e, err));  // час 25
    CHECK(!parseEventLine("2030-05-17 09:61 текст", e, err));  // минута 61
    CHECK(!parseEventLine("2030-05-17 9:30 текст", e, err));   // нет ведущего нуля
    CHECK(!parseEventLine("2030-05-17 09-30 текст", e, err));
    CHECK(!err.empty());
}

static void testOccurrenceOnce() {
    Event e;
    std::string err;
    CHECK(parseEventLine("2030-05-17 09:30 once", e, err));
    CHECK(firstOccurrenceAfter(e, at(2030, 5, 17, 9, 0)) == e.start);
    CHECK(firstOccurrenceAfter(e, e.start) == NO_OCCURRENCE);      // строго позже
    CHECK(firstOccurrenceAfter(e, e.start + 1) == NO_OCCURRENCE);
    CHECK(firstOccurrenceAfter(e, at(2031, 1, 1, 0, 0)) == NO_OCCURRENCE);
}

static void testOccurrenceHourly() {
    Event e;
    std::string err;
    CHECK(parseEventLine("2030-05-17 09:30 -h hourly", e, err));
    CHECK(firstOccurrenceAfter(e, at(2030, 5, 17, 9, 29)) == at(2030, 5, 17, 9, 30));
    CHECK(firstOccurrenceAfter(e, at(2030, 5, 17, 9, 30)) == at(2030, 5, 17, 10, 30));
    CHECK(firstOccurrenceAfter(e, at(2030, 5, 17, 10, 29, 59)) == at(2030, 5, 17, 10, 30));
    CHECK(firstOccurrenceAfter(e, at(2030, 5, 18, 0, 0)) == at(2030, 5, 18, 0, 30));
}

static void testOccurrenceDaily() {
    Event e;
    std::string err;
    CHECK(parseEventLine("2030-05-17 09:30 -d daily", e, err));
    CHECK(firstOccurrenceAfter(e, at(2030, 5, 17, 9, 30)) == at(2030, 5, 18, 9, 30));
    CHECK(firstOccurrenceAfter(e, at(2030, 5, 17, 23, 0)) == at(2030, 5, 18, 9, 30));
    CHECK(firstOccurrenceAfter(e, at(2030, 5, 31, 10, 0)) == at(2030, 6, 1, 9, 30));  // смена месяца
    CHECK(firstOccurrenceAfter(e, at(2031, 1, 1, 0, 0)) == at(2031, 1, 1, 9, 30));    // смена года
}

static void testOccurrenceWeekly() {
    Event e;
    std::string err;
    CHECK(parseEventLine("2030-05-17 09:30 -w weekly", e, err));
    CHECK(firstOccurrenceAfter(e, at(2030, 5, 17, 9, 30)) == at(2030, 5, 24, 9, 30));
    CHECK(firstOccurrenceAfter(e, at(2030, 5, 30, 0, 0)) == at(2030, 5, 31, 9, 30));
    CHECK(firstOccurrenceAfter(e, at(2030, 6, 1, 0, 0)) == at(2030, 6, 7, 9, 30));
}

static void testLoadEvents() {
    std::istringstream in(
        "# комментарий\n"
        "\n"
        "2030-05-17 09:30 первое\n"
        "битая строка\r\n"
        "   # тоже комментарий\n"
        "2030-05-18 10:00 -w второе\r\n"
        "2030-02-31 10:00 плохая дата\n");
    std::vector<Event> events;
    std::vector<std::string> errors;
    loadEvents(in, events, errors);
    CHECK(events.size() == 2);
    CHECK(errors.size() == 2);
    CHECK(events[0].text == "первое");
    CHECK(events[1].repeat == Repeat::Weekly);
    CHECK(events[1].text == "второе");  // '\r' отброшен
    CHECK(errors[0].find("строка 4") != std::string::npos);
    CHECK(errors[1].find("строка 7") != std::string::npos);
}

static void testLoadFile() {
    std::vector<Event> events;
    std::vector<std::string> errors;
    CHECK(!loadEventsFromFile("/nonexistent/path/reminder.conf", events, errors));
    CHECK(errors.size() == 1);
    CHECK(events.empty());
}

static void testKey() {
    Event a, b, c;
    std::string err;
    CHECK(parseEventLine("2030-05-17 09:30 текст", a, err));
    CHECK(parseEventLine("2030-05-17 09:30   текст", b, err));
    CHECK(parseEventLine("2030-05-17 09:30 -d текст", c, err));
    CHECK(a.key() == b.key());
    CHECK(a.key() != c.key());
}

int main() {
    testParseValid();
    testFlagLikeText();
    testParseInvalid();
    testOccurrenceOnce();
    testOccurrenceHourly();
    testOccurrenceDaily();
    testOccurrenceWeekly();
    testLoadEvents();
    testLoadFile();
    testKey();
    std::cout << "Модульные тесты: проверок " << g_total << ", провалено " << g_failed << "\n";
    return g_failed == 0 ? 0 : 1;
}
