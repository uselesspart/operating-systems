// Событие-напоминание и разбор строк конфигурационного файла.
// Этот модуль не зависит от системных вызовов демона, поэтому его удобно тестировать отдельно.
#pragma once

#include <ctime>
#include <istream>
#include <string>
#include <vector>

enum class Repeat { None, Hourly, Daily, Weekly };

struct Event {
    std::time_t start = 0;          // время первого срабатывания (локальное время)
    Repeat repeat = Repeat::None;   // период повтора
    std::string text;               // текст напоминания

    // Ключ, идентифицирующий событие (нужен, чтобы после SIGHUP не напоминать повторно)
    std::string key() const;
};

constexpr std::time_t NO_OCCURRENCE = static_cast<std::time_t>(-1);

// Разбирает строку "YYYY-MM-DD HH:MM[:SS] [-h|-d|-w] текст".
// При ошибке возвращает false и записывает описание в error.
bool parseEventLine(const std::string& line, Event& out, std::string& error);

// Ближайшее срабатывание события СТРОГО позже момента after,
// либо NO_OCCURRENCE, если событие больше не наступит.
std::time_t firstOccurrenceAfter(const Event& ev, std::time_t after);

// Читает все события из потока. Пустые строки и строки, начинающиеся с '#', пропускаются.
// Некорректные строки не прерывают чтение, их описания попадают в errors.
void loadEvents(std::istream& in, std::vector<Event>& events, std::vector<std::string>& errors);

// То же самое для файла. Возвращает false, если файл не удалось открыть (причина в errors[0]).
bool loadEventsFromFile(const std::string& path, std::vector<Event>& events,
                        std::vector<std::string>& errors);
