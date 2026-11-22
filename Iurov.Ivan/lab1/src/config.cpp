#include "treecleaner/config.hpp"

#include <charconv>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace treecleaner {

std::vector<ConfigEntry> loadConfig(const fs::path& configPath) {
    std::ifstream input(configPath);
    if (!input) {
        throw std::runtime_error("не удалось открыть конфигурационный файл: " + configPath.string());
    }

    std::error_code ec;
    fs::path absoluteConfig = fs::absolute(configPath, ec);
    if (ec) {
        throw std::runtime_error("не удалось определить абсолютный путь конфигурации: " + ec.message());
    }
    const fs::path baseDirectory = absoluteConfig.lexically_normal().parent_path();

    std::vector<ConfigEntry> entries;
    std::string line;
    std::size_t lineNumber = 0;
    constexpr std::size_t maxDepth = 1024;

    while (std::getline(input, line)) {
        ++lineNumber;
        const auto commentPosition = line.find('#');
        if (commentPosition != std::string::npos) {
            line.erase(commentPosition);
        }

        std::istringstream row(line);
        std::string folderText;
        std::string depthText;
        std::string extra;
        if (!(row >> folderText)) {
            continue;
        }
        if (!(row >> depthText) || (row >> extra)) {
            throw std::runtime_error("ошибка конфигурации, строка " + std::to_string(lineNumber) +
                                     ": ожидается ровно два поля - folder depth");
        }

        std::size_t depth = 0;
        const char* begin = depthText.data();
        const char* end = begin + depthText.size();
        const auto conversion = std::from_chars(begin, end, depth);
        if (conversion.ec != std::errc{} || conversion.ptr != end || depth == 0 || depth > maxDepth) {
            throw std::runtime_error("ошибка конфигурации, строка " + std::to_string(lineNumber) +
                                     ": depth должен быть целым числом от 1 до " + std::to_string(maxDepth));
        }

        fs::path folder(folderText);
        if (folder.is_relative()) {
            folder = baseDirectory / folder;
        }
        folder = folder.lexically_normal();
        entries.push_back(ConfigEntry{folder, depth});
    }

    if (input.bad()) {
        throw std::runtime_error("ошибка чтения конфигурационного файла: " + configPath.string());
    }
    return entries;
}

} // namespace treecleaner
