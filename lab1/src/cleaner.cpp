#include "cleaner.hpp"

#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

CleanResult removeSubdirectories(const std::string& dir) {
    CleanResult result;
    std::error_code ec;

    fs::directory_iterator it(dir, ec);
    if (ec) {
        result.error = "cannot open " + dir + ": " + ec.message();
        return result;
    }

    std::vector<fs::path> targets;
    const fs::directory_iterator end;
    while (it != end) {
        std::error_code e;
        const auto st = it->symlink_status(e);  // symlink_status: ссылки не разыменовываем
        if (!e && fs::is_directory(st)) targets.push_back(it->path());
        it.increment(ec);
        if (ec) {
            result.error = "cannot read " + dir + ": " + ec.message();
            break;
        }
    }

    for (const auto& t : targets) {
        std::error_code e;
        fs::remove_all(t, e);
        if (e) {
            ++result.failed;
            if (result.error.empty()) result.error = "cannot remove " + t.string() + ": " + e.message();
        } else {
            ++result.removed;
        }
    }
    return result;
}
