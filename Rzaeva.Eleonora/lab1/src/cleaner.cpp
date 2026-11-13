#include "cleaner.hpp"

#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace dirclean {

bool hasIgnFile(const fs::path& ignFile) {
    std::error_code ec;
    return fs::exists(ignFile, ec);
}

std::size_t cleanFolderContents(const fs::path& folder) {
    std::vector<fs::path> entries;
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; it != end; it.increment(ec)) {
        if (ec) throw fs::filesystem_error("cannot iterate folder", folder, ec);
        entries.push_back(it->path());
    }

    std::size_t removed = 0;
    for (const fs::path& entry : entries) {
        std::error_code removeEc;
        removed += fs::remove_all(entry, removeEc);
    }
    return removed;
}

}
