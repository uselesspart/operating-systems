#include "ConfigLoader.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include <vector>

namespace
{
    class TemporaryConfig
    {
    public:
        explicit TemporaryConfig(const std::string& content)
        {
            std::string pattern =
                (std::filesystem::temp_directory_path()
                    / "mydaemon-config-XXXXXX").string();

            std::vector<char> name(pattern.begin(), pattern.end());
            name.push_back('\0');

            const int fd = mkstemp(name.data());

            if (fd == -1)
            {
                throw std::runtime_error("Cannot create test config");
            }

            close(fd);
            path_ = name.data();

            std::ofstream file(path_);
            file << content;
            file.close();

            if (!file)
            {
                std::error_code error;
                std::filesystem::remove(path_, error);
                throw std::runtime_error("Cannot write test config");
            }
        }

        ~TemporaryConfig() noexcept
        {
            std::error_code error;
            std::filesystem::remove(path_, error);
        }

        TemporaryConfig(const TemporaryConfig&) = delete;
        TemporaryConfig& operator=(const TemporaryConfig&) = delete;

        const std::filesystem::path& path() const
        {
            return path_;
        }

    private:
        std::filesystem::path path_;
    };

    void require(bool condition, const std::string& message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    Config loadConfig(const std::string& content)
    {
        const TemporaryConfig file(content);
        return ConfigLoader::load(file.path());
    }

    void expectInvalid(const std::string& content)
    {
        // Создание файла не входит в проверяемую операцию.
        const TemporaryConfig file(content);
        bool rejected = false;

        try
        {
            ConfigLoader::load(file.path());
        }
        catch (const std::runtime_error&)
        {
            rejected = true;
        }

        require(rejected, "Invalid config was accepted");
    }

    void testValidConfig()
    {
        const Config config = loadConfig(
            "interval=10\n"
            "add_event 2030-01-15 12:30 Проверить лабораторную\n"
        );

        require(config.interval == std::chrono::seconds(10),
            "Incorrect interval");
        require(config.events.size() == 1, "Incorrect event count");

        const ReminderEvent& event = config.events.front();

        require(event.text == "Проверить лабораторную",
            "Incorrect event text");
        require(event.repeat == Repeat::None, "Unexpected repeat");
        require(!event.key.empty(), "Event key is empty");

        const std::time_t time =
            std::chrono::system_clock::to_time_t(event.time);
        const std::tm* local = std::localtime(&time);

        require(local != nullptr, "Cannot convert event time");

        std::ostringstream formatted;
        formatted << std::put_time(local, "%Y-%m-%d %H:%M");

        require(formatted.str() == "2030-01-15 12:30",
            "Incorrect event date/time");
    }

    void testRepeatFlags()
    {
        const Config config = loadConfig(
            "interval=10\n"
            "add_event 2030-01-15 12:30 -h Hourly reminder\n"
            "add_event 2030-01-15 12:30 -d Daily reminder\n"
            "add_event 2030-01-15 12:30 -w Weekly reminder\n"
        );

        require(config.events.size() == 3, "Incorrect event count");
        require(config.events[0].repeat == Repeat::Hourly,
            "Incorrect hourly repeat");
        require(config.events[1].repeat == Repeat::Daily,
            "Incorrect daily repeat");
        require(config.events[2].repeat == Repeat::Weekly,
            "Incorrect weekly repeat");
        require(config.events[0].text == "Hourly reminder",
            "Hourly flag was not removed from text");
        require(config.events[1].text == "Daily reminder",
            "Daily flag was not removed from text");
        require(config.events[2].text == "Weekly reminder",
            "Weekly flag was not removed from text");
    }

    void testCommentsAndEmptyLines()
    {
        const Config config = loadConfig(
            "\n"
            "   # Test configuration\n"
            "interval=10\n"
            "   \n"
            "  add_event 2030-01-15 12:30 Test reminder   \n"
        );

        require(config.events.size() == 1, "Incorrect event count");
        require(config.events.front().text == "Test reminder",
            "Whitespace was not removed");
    }

    void testInvalidIntervals()
    {
        expectInvalid("# Interval is missing\n");
        expectInvalid("interval=0\n");
        expectInvalid("interval=-10\n");
        expectInvalid("interval=abc\n");
    }

    void testInvalidEvents()
    {
        expectInvalid(
            "interval=10\n"
            "add_event 2030-02-30 12:30 Invalid date\n"
        );
        expectInvalid(
            "interval=10\n"
            "unknown 2030-01-15 12:30 Unknown command\n"
        );
        expectInvalid(
            "interval=10\n"
            "add_event 2030-01-15 12:30\n"
        );
        expectInvalid(
            "interval=10\n"
            "add_event 2030-01-15 12:30 -h\n"
        );
    }

    void runTest(
        const std::string& name,
        void (*test)(),
        int& passed,
        int& failed)
    {
        try
        {
            test();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        }
        catch (const std::exception& e)
        {
            ++failed;
            std::cerr << "[FAIL] " << name << ": " << e.what() << '\n';
        }
    }
}

int main()
{
    int passed = 0;
    int failed = 0;

    runTest("Valid config", testValidConfig, passed, failed);
    runTest("Repeat flags", testRepeatFlags, passed, failed);
    runTest("Comments and empty lines",
        testCommentsAndEmptyLines, passed, failed);
    runTest("Invalid intervals", testInvalidIntervals, passed, failed);
    runTest("Invalid events", testInvalidEvents, passed, failed);

    std::cout << "Passed: " << passed << ", failed: " << failed << '\n';

    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
