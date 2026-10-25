#include "cleaner.hpp"

#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {

    void clean_directory(
    const fs::path& directory,
    int current_depth,
    int max_depth
) {
        std::error_code error;
        fs::directory_iterator iterator(directory, error);

        if (error) {
            throw std::runtime_error(
                "Cannot read directory: " + directory.string()
            );
        }

        for (const fs::directory_entry& entry : iterator) {
            const fs::file_status status = entry.symlink_status(error);

            if (error) {
                throw std::runtime_error(
                    "Cannot inspect entry: " + entry.path().string()
                );
            }

            if (fs::is_directory(status)) {
                if (current_depth < max_depth) {
                    clean_directory(
                        entry.path(),
                        current_depth + 1,
                        max_depth
                    );
                }

                continue;
            }

            // Файлы непосредственно в folder сохраняются.
            if (current_depth == 0) {
                continue;
            }

            fs::remove(entry.path(), error);

            if (error) {
                throw std::runtime_error(
                    "Cannot remove entry: " + entry.path().string()
                );
            }
        }
    }

} // namespace

void Cleaner::clean(const fs::path& folder, int max_depth) {
    if (max_depth <= 0) {
        throw std::invalid_argument("Depth must be positive");
    }

    std::error_code error;
    const fs::file_status status = fs::symlink_status(folder, error);

    if (error) {
        throw std::runtime_error(
            "Cannot inspect folder: " + folder.string()
        );
    }

    if (!fs::is_directory(status)) {
        throw std::invalid_argument(
            "Configured path is not a directory: " + folder.string()
        );
    }

    clean_directory(folder, 0, max_depth);
}