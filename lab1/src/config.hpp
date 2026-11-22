#pragma once
#include <string>

// Настройки демона, прочитанные из конфигурационного файла.
struct Config {
    unsigned interval = 0;  // период между проверками, секунды
    std::string dir1;       // папка 1: из неё удаляются все подпапки (вариант 7)
    std::string dir2;       // папка 2 (необязательная): разбирается, но не изменяется
};

// Разбор конфигурационного файла. Относительные пути приводятся к абсолютным
// относительно папки, где лежит сам конфиг. При ошибке возвращает false и заполняет error.
bool parseConfig(const std::string& path, Config& out, std::string& error);

// Абсолютный нормализованный путь (файл не обязан существовать).
std::string absolutePath(const std::string& path);
