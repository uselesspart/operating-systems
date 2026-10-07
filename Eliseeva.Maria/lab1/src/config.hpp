#pragma once

#include <istream>
#include <string>
#include <vector>

namespace dirclean {

constexpr unsigned DEFAULT_INTERVAL = 30;  // секунды

// Одна строка конфига: "<folder> <ignfile>".
struct Rule {
    std::string folder;   // абсолютный путь к папке
    std::string ignfile;  // имя файла-"защиты" (или абсолютный путь)
};

struct Config {
    std::vector<Rule> rules;
    unsigned interval = DEFAULT_INTERVAL;
};

// Разбор конфига из потока. Относительные folder считаются от baseDir
// Некорректные строки пропускаются.
Config parseConfig(std::istream &in, const std::string &baseDir,
                   std::vector<std::string> *warnings = nullptr);

// Чтение конфига из файла. false, если файл не удалось открыть (out не меняется).
bool loadConfigFile(const std::string &path, Config &out,
                    std::vector<std::string> *warnings = nullptr);

}  
