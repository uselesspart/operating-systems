#pragma once
#include <cstddef>
#include <string>

struct CleanResult {
    std::size_t removed = 0;  // сколько подпапок удалено
    std::size_t failed = 0;   // сколько не удалось удалить
    std::string error;        // первое сообщение об ошибке (пусто, если всё хорошо)
    bool ok() const { return error.empty(); }
};

// Удаляет все подпапки первого уровня (вместе с содержимым). Обычные файлы
// и символические ссылки в папке dir остаются нетронутыми, ссылки не разыменовываются.
CleanResult removeSubdirectories(const std::string& dir);
