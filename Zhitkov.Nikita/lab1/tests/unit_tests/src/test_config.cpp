#include "test_config.h"
#include "Config.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

const char* kPath = "/tmp/reminder_test_config.ini";

void writeFile(const std::string& content) {
    std::ofstream out(kPath);
    out << content;
}

// Все ошибки парсера выражаются через std::runtime_error
// Хелпер убирает дублирование try/catch по тестам
bool throwsOnParse(const std::string& content) {
    writeFile(content);
    try {
        parseConfig(kPath);
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

}

void testValidConfig() {
    writeFile(
        "# sample\n"
        "interval = 15\n"
        "events_file = /tmp/events.txt\n");
    const Config cfg = parseConfig(kPath);
    assert(cfg.intervalSeconds == 15);
    assert(cfg.eventsFile == "/tmp/events.txt");
}

void testWhitespaceTolerance() {
    writeFile("interval   =   3\n"
              "events_file=   rel/path.txt\n");
    const Config cfg = parseConfig(kPath);
    assert(cfg.intervalSeconds == 3);
    assert(cfg.eventsFile == "rel/path.txt");
}

void testMissingEventsFile() {
    assert(throwsOnParse("interval = 5\n"));
}

void testInvalidInterval() {
    assert(throwsOnParse("interval = abc\n"
                         "events_file = /tmp/x\n"));
}

void testNegativeInterval() {
    assert(throwsOnParse("interval = -1\n"
                         "events_file = /tmp/x\n"));
}

void testZeroInterval() {
    assert(throwsOnParse("interval = 0\n"
                         "events_file = /tmp/x\n"));
}

void testMissingFile() {
    bool thrown = false;
    try {
        parseConfig("/tmp/this_file_definitely_does_not_exist_987654.ini");
    } catch (const std::runtime_error&) {
        thrown = true;
    }
    assert(thrown);

    std::remove(kPath);
}