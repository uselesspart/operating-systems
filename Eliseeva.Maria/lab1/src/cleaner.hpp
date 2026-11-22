#pragma once

#include "config.hpp"

#include <cstddef>
#include <string>

namespace dirclean {

enum class CleanStatus {
    Cleaned,       // файла-защиты нет, содержимое папки удалено
    Ignored,       // файл-защита найден, ничего не сделано
    NotDirectory,  // folder не существует или не каталог
    Refused,       // отказ: корневой каталог чистить нельзя
    Error          // не удалось определить наличие файла-защиты или прочитать папку
};

struct CleanReport {
    CleanStatus status = CleanStatus::Error;
    std::string folder;       // абсолютный путь, который обрабатывался
    std::size_t removed = 0;  // удалено элементов верхнего уровня
    std::size_t failed = 0;   // не удалось удалить
    std::string message;      // подробности для журнала
};

// удалить содержимое папки, если в ней нет файла ignfile.
CleanReport applyRule(const Rule &rule);

}  
