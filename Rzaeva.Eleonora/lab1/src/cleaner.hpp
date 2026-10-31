#pragma once

#include <cstddef>
#include <filesystem>

namespace dirclean {

bool hasIgnFile(const std::filesystem::path& ignFile);

// Удаляет содержимое каталога, оставляя сам каталог. Возвращает число удалённых записей.
std::size_t cleanFolderContents(const std::filesystem::path& folder);

}
