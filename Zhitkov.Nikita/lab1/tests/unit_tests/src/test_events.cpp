#include "test_events.h"
#include "Event.h"

#include <cassert>
#include <ctime>
#include <string>

namespace {

Event makeEvent(std::time_t time, char flag, const std::string& text) {
    Event ev;
    ev.time      = time;
    ev.flag      = flag;
    ev.text      = text;
    ev.lastFired = 0;
    return ev;
}

}

void testParseEventLine() {
    Event ev;

    // Однократное событие без флага
    assert(parseEventLine("15.01.2025 14:30:00 Meeting with Bob", ev));
    assert(ev.flag == 0);
    assert(ev.text == "Meeting with Bob");

    // Каждый час
    assert(parseEventLine("15.01.2025 14:30:00 -h Hourly reminder", ev));
    assert(ev.flag == 'h');
    assert(ev.text == "Hourly reminder");
    assert(ev.repeatSeconds() == 3600);

    // Каждый день
    assert(parseEventLine("15.01.2025 14:30:00 -d Daily standup", ev));
    assert(ev.flag == 'd');
    assert(ev.repeatSeconds() == 86400);

    // Каждую неделю
    assert(parseEventLine("15.01.2025 14:30:00 -w Weekly report", ev));
    assert(ev.flag == 'w');
    assert(ev.repeatSeconds() == 604800);

    // Флаг в начале текста без пробела - это часть текста, а не флаг
    assert(parseEventLine("15.01.2025 14:30:00 -hello world", ev));
    assert(ev.flag == 0);
    assert(ev.text == "-hello world");

    // Некорректные строки
    assert(!parseEventLine("not a date", ev));
    assert(!parseEventLine("2025-01-15 14:30:00 wrong separator", ev));
    assert(!parseEventLine("15/01/2025 14:30:00 wrong separator", ev));
}

void testOneTimeEvent() {
    const std::time_t base  = 1'000'000;
    const std::time_t grace = 10;

    const Event oneTime = makeEvent(base, 0, "one-shot");

    // Ещё рано
    assert(decideEventAction(oneTime, 0, base - 10, grace) == EventAction::Skip);

    // Ровно в момент наступления
    assert(decideEventAction(oneTime, 0, base, grace) == EventAction::Fire);

    // Через 5 секунд - ещё в пределах grace
    assert(decideEventAction(oneTime, 0, base + 5, grace) == EventAction::Fire);

    // Ровно на границе grace (now - due == grace) - всё ещё Fire
    assert(decideEventAction(oneTime, 0, base + grace, grace) == EventAction::Fire);

    // На секунду позже границы - уже MarkStale
    assert(decideEventAction(oneTime, 0, base + grace + 1, grace)
           == EventAction::MarkStale);

    // Существенно позже - тоже MarkStale
    assert(decideEventAction(oneTime, 0, base + 3600, grace)
           == EventAction::MarkStale);

    // Уже отстрелялось (lastFired != 0) - Skip независимо от now
    assert(decideEventAction(oneTime, base, base + 1, grace) == EventAction::Skip);
    assert(decideEventAction(oneTime, base, base + 1000, grace) == EventAction::Skip);
}

void testHourlyEvent() {
    const std::time_t base  = 1'000'000;
    const std::time_t grace = 10;
    const std::time_t hour  = 3600;

    const Event hourly = makeEvent(base, 'h', "hourly");

    // До первого срабатывания ещё рано
    assert(decideEventAction(hourly, 0, base - 5, grace) == EventAction::Skip);

    // Первое срабатывание ровно в base
    assert(decideEventAction(hourly, 0, base, grace) == EventAction::Fire);

    // До следующего часа - Skip
    assert(decideEventAction(hourly, base, base + 100, grace) == EventAction::Skip);
    assert(decideEventAction(hourly, base, base + hour - 10, grace)
           == EventAction::Skip);

    // Прошёл ровно час - снова Fire
    assert(decideEventAction(hourly, base, base + hour, grace) == EventAction::Fire);

    // Час + 5 секунд - ещё Fire
    assert(decideEventAction(hourly, base, base + hour + 5, grace)
           == EventAction::Fire);

    // Час + grace + 1 - MarkStale
    assert(decideEventAction(hourly, base, base + hour + grace + 1, grace)
           == EventAction::MarkStale);

    // Демон вернулся ровно на 2h-границу - событие срабатывает по расписанию
    assert(decideEventAction(hourly, base, base + 2 * hour, grace)
           == EventAction::Fire);
}

void testDailyEvent() {
    const std::time_t base  = 1'000'000;
    const std::time_t grace = 10;
    const std::time_t day   = 86400;

    const Event daily = makeEvent(base, 'd', "daily");

    assert(decideEventAction(daily, base, base + day - 1, grace)
           == EventAction::Skip);
    assert(decideEventAction(daily, base, base + day, grace)
           == EventAction::Fire);
    assert(decideEventAction(daily, base, base + day + 5, grace)
           == EventAction::Fire);
    assert(decideEventAction(daily, base, base + day + grace + 1, grace)
           == EventAction::MarkStale);
}

void testWeeklyEvent() {
    const std::time_t base  = 1'000'000;
    const std::time_t grace = 10;
    const std::time_t week  = 604800;

    const Event weekly = makeEvent(base, 'w', "weekly");

    assert(decideEventAction(weekly, base, base + week - 1, grace)
           == EventAction::Skip);
    assert(decideEventAction(weekly, base, base + week, grace)
           == EventAction::Fire);
    assert(decideEventAction(weekly, base, base + week + grace + 1, grace)
           == EventAction::MarkStale);
}

void testRecurringScheduleAlignment() {
    const std::time_t base  = 1'000'000;
    const std::time_t grace = 10;
    const std::time_t hour  = 3600;

    const Event hourly = makeEvent(base, 'h', "hourly");

    // Демон спал 3 часа 15 секунд. Ожидаем: due продвинулся к base+3h (граница), опоздание 15 > grace -> MarkStale, fired=base+3h
    std::time_t due = 0;
    assert(decideEventAction(hourly, base, base + 3 * hour + 15, grace, &due)
           == EventAction::MarkStale);
    assert(due == base + 3 * hour);

    // То же, но 5 секунд опоздания - в пределах grace -> Fire
    assert(decideEventAction(hourly, base, base + 3 * hour + 5, grace, &due)
           == EventAction::Fire);
    assert(due == base + 3 * hour);
}

void testGraceVariants() {
    const std::time_t base = 1'000'000;
    const Event oneTime = makeEvent(base, 0, "x");

    // grace = 0: срабатывает только ровно в момент due
    assert(decideEventAction(oneTime, 0, base,     0) == EventAction::Fire);
    assert(decideEventAction(oneTime, 0, base + 1, 0) == EventAction::MarkStale);

    // Большой grace - догон длится долго
    assert(decideEventAction(oneTime, 0, base + 600, 600) == EventAction::Fire);
    assert(decideEventAction(oneTime, 0, base + 601, 600) == EventAction::MarkStale);
}