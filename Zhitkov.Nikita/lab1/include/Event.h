#pragma once

#include <ctime>
#include <string>
#include <vector>

struct Event {
    std::time_t time = 0;        // когда напомнить
    char        flag = 0;        // 0 | 'h' | 'd' | 'w'
    std::string text;
    std::time_t lastFired = 0;   // когда сработало в последний раз (0 = никогда)

    std::time_t repeatSeconds() const;
};

// Разбирает одну строку файла событий. Возвращает false, если строка некорректна
bool parseEventLine(const std::string& line, Event& out);

// Читает все события из файла. Отсутствующий файл трактуется как пустой список
std::vector<Event> parseEventsFile(const std::string& path);

enum class EventAction {
    Skip,      // ещё рано (или одноразовое уже отстреляно) - ничего не делать
    Fire,      // сработать
    MarkStale  // опоздало сильнее grace - тихо пометить как отработанное
};

// outDue (опционально) - сюда возвращается due, к которому нужно привязать
// fired_, чтобы сохранить расписание
EventAction decideEventAction(const Event& ev,
                              std::time_t lastFired,
                              std::time_t now,
                              std::time_t grace,
                              std::time_t* outDue = nullptr);