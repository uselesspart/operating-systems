#include "ReminderTask.hpp"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <syslog.h>
#include <unistd.h>


std::optional<
    std::chrono::system_clock::time_point
>
ReminderTask::getCurrentOccurrence(
    const ReminderEvent& event,
    std::chrono::system_clock::time_point now
) const
{
    using namespace std::chrono;

    //Событие ещё не наступило.
    if (now < event.time)
    {
        return std::nullopt;
    }

    //Одноразовое событие
    if (event.repeat == Repeat::None)
    {
        return event.time;
    }


    seconds period{0};

    switch (event.repeat)
    {
        case Repeat::Hourly:
            period = hours(1);
            break;

        case Repeat::Daily:
            period = hours(24);
            break;

        case Repeat::Weekly:
            period = hours(24 * 7);
            break;

        case Repeat::None:
            break;
    }


    const auto elapsed =
        duration_cast<seconds>(
            now - event.time
        );


    const auto occurrenceNumber =
        elapsed.count() / period.count();


    return event.time
        + period * occurrenceNumber;
}


void ReminderTask::execute(
    const Config& config)
{
    const auto now =
        std::chrono::system_clock::now();


    for (const ReminderEvent& event :
         config.events)
    {
        const auto occurrence =
            getCurrentOccurrence(
                event,
                now
            );


        if (!occurrence.has_value())
        {
            continue;
        }


        const std::time_t occurrenceId =
            std::chrono::system_clock::to_time_t(
                occurrence.value()
            );


        const auto found =
            lastFired_.find(event.key);


        
        //Это occurrence мы уже показывали. 
        if (
            found != lastFired_.end() &&
            found->second == occurrenceId)
        {
            continue;
        }


        /*
         * Пытаемся показать напоминание.
         *
         * Если fork не получится —
         * exception уйдёт наружу и событие
         * НЕ будет отмечено выполненным.
         */
        showReminder(event);


        lastFired_[event.key] =
            occurrenceId;
    }
}


void ReminderTask::showReminder(
    const ReminderEvent& event) const
{
    const pid_t pid = ::fork();

    if (pid == -1)
    {
        throw std::runtime_error(
            std::string(
                "Cannot fork reminder process: "
            )
            + std::strerror(errno)
        );
    }


    /*
     * Parent — наш daemon.
     *
     * Он НЕ ждёт terminal.
     */
    if (pid > 0)
    {
        return;
    }


    /*
     * CHILD.
     *
     * event.text НЕ вставляется прямо
     * в shell-команду.
     *
     * Она передаётся как $1,
     * поэтому текст не превращается
     * в shell-команды.
     */
    constexpr const char* command =
        "printf '\\n===== REMINDER =====\\n\\n%s\\n\\n' \"$1\"; "
        "printf 'Press Enter to close...'; "
        "read -r _";


    /*
     * Сначала пробуем стандартное
     * Debian/Ubuntu имя terminal emulator.
     */
    execlp(
        "x-terminal-emulator",
        "x-terminal-emulator",
        "-e",
        "bash",
        "-c",
        command,
        "reminder",
        event.text.c_str(),
        static_cast<char*>(nullptr)
    );


    /*
     * Если exec вернулся — первый terminal
     * не запустился.
     *
     * Пробуем GNOME Terminal.
     */
    execlp(
        "gnome-terminal",
        "gnome-terminal",
        "--",
        "bash",
        "-c",
        command,
        "reminder",
        event.text.c_str(),
        static_cast<char*>(nullptr)
    );


    /*
     * Если мы вообще дошли сюда —
     * ни один exec не сработал.
     */
    syslog(
        LOG_ERR,
        "Cannot start terminal emulator: %s",
        std::strerror(errno)
    );


    _exit(127);
}