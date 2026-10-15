#include "ConfigLoader.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>


std::string ConfigLoader::trim(
    const std::string& value)
{
    const auto first = std::find_if_not(
        value.begin(),
        value.end(),
        [](unsigned char ch)
        {
            return std::isspace(ch);
        }
    );

    const auto last = std::find_if_not(
        value.rbegin(),
        value.rend(),
        [](unsigned char ch)
        {
            return std::isspace(ch);
        }
    ).base();

    if (first >= last)
    {
        return {};
    }

    return std::string(first, last);
}


std::chrono::system_clock::time_point
ConfigLoader::parseDateTime(
    const std::string& date,
    const std::string& time)
{
    const std::string input =
        date + " " + time;

    std::tm tm{};

    std::istringstream stream(input);

    stream >> std::get_time(
        &tm,
        "%Y-%m-%d %H:%M"
    );

    if (stream.fail())
    {
        throw std::runtime_error(
            "Invalid event date/time: " + input
        );
    }

    /*
     * Просим mktime самостоятельно определить,
     * действует ли сейчас DST.
     */
    tm.tm_isdst = -1;

    const std::time_t rawTime =
        std::mktime(&tm);

    if (rawTime == static_cast<std::time_t>(-1))
    {
        throw std::runtime_error(
            "Cannot convert event date/time: " + input
        );
    }

    /*
     * mktime может нормализовать что-то вроде:
     *
     * 2026-02-31
     *
     * Поэтому проверяем результат обратно.
     */
    const std::tm* checked =
        std::localtime(&rawTime);

    if (checked == nullptr)
    {
        throw std::runtime_error(
            "Cannot validate event date/time"
        );
    }

    std::ostringstream checkStream;

    checkStream << std::put_time(
        checked,
        "%Y-%m-%d %H:%M"
    );

    if (checkStream.str() != input)
    {
        throw std::runtime_error(
            "Invalid event date/time: " + input
        );
    }

    return std::chrono::system_clock::from_time_t(
        rawTime
    );
}


Config ConfigLoader::load(
    const std::filesystem::path& path)
{
    std::ifstream file(path);

    if (!file.is_open())
    {
        throw std::runtime_error(
            "Cannot open config file"
        );
    }

    Config config;

    bool intervalFound = false;

    std::string line;
    std::size_t lineNumber = 0;

    while (std::getline(file, line))
    {
        ++lineNumber;

        line = trim(line);

        if (line.empty())
        {
            continue;
        }

        /*
         * Разрешаем комментарии.
         */
        if (line[0] == '#')
        {
            continue;
        }

        /*
         * interval=10
         */
        if (line.rfind("interval=", 0) == 0)
        {
            const std::string value =
                trim(line.substr(9));

            int seconds = 0;

            try
            {
                seconds = std::stoi(value);
            }
            catch (const std::exception&)
            {
                throw std::runtime_error(
                    "Invalid interval on line "
                    + std::to_string(lineNumber)
                );
            }

            if (seconds <= 0)
            {
                throw std::runtime_error(
                    "Interval must be positive"
                );
            }

            config.interval =
                std::chrono::seconds(seconds);

            intervalFound = true;

            continue;
        }

        /*
         * Остальные строки должны иметь вид:
         *
         * add_event date time [flag] text
         */

        std::istringstream stream(line);

        std::string command;
        std::string date;
        std::string time;

        if (!(stream >> command >> date >> time))
        {
            throw std::runtime_error(
                "Invalid config line "
                + std::to_string(lineNumber)
            );
        }

        if (command != "add_event")
        {
            throw std::runtime_error(
                "Unknown command on line "
                + std::to_string(lineNumber)
            );
        }

        std::string rest;

        std::getline(
            stream >> std::ws,
            rest
        );

        rest = trim(rest);

        if (rest.empty())
        {
            throw std::runtime_error(
                "Event text is empty on line "
                + std::to_string(lineNumber)
            );
        }

        Repeat repeat = Repeat::None;


        auto removeFlag =
            [&rest](const std::string& flag)
            {
                rest = ConfigLoader::trim(
                    rest.substr(flag.size())
                );
            };


        if (
            rest == "-h" ||
            rest.rfind("-h ", 0) == 0)
        {
            repeat = Repeat::Hourly;
            removeFlag("-h");
        }
        else if (
            rest == "-d" ||
            rest.rfind("-d ", 0) == 0)
        {
            repeat = Repeat::Daily;
            removeFlag("-d");
        }
        else if (
            rest == "-w" ||
            rest.rfind("-w ", 0) == 0)
        {
            repeat = Repeat::Weekly;
            removeFlag("-w");
        }


        if (rest.empty())
        {
            throw std::runtime_error(
                "Event text is empty on line "
                + std::to_string(lineNumber)
            );
        }


        ReminderEvent event;

        /*
         * Скрипт только ДОБАВЛЯЕТ строки,
         * поэтому номер строки + содержимое
         * достаточно удобно использовать как key.
         */
        event.key =
            std::to_string(lineNumber)
            + ":"
            + line;

        event.time =
            parseDateTime(date, time);

        event.repeat = repeat;

        event.text = rest;


        config.events.push_back(
            std::move(event)
        );
    }


    if (!intervalFound)
    {
        throw std::runtime_error(
            "Config does not contain interval"
        );
    }


    return config;
}