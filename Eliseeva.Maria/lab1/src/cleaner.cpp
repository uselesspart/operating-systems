#include "cleaner.hpp"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <sys/stat.h>
#include <vector>

namespace fs = std::filesystem;

namespace dirclean {

CleanReport applyRule(const Rule &rule) {
    CleanReport rep;
    std::error_code ec;

    fs::path folder = fs::absolute(rule.folder, ec);
    rep.folder = folder.string();
    if (ec || !fs::is_directory(folder, ec)) {
        rep.status = CleanStatus::NotDirectory;
        return rep;
    }

    folder = fs::weakly_canonical(folder, ec);
    if (ec) {
        rep.status = CleanStatus::NotDirectory;
        return rep;
    }
    rep.folder = folder.string();

    if (folder == folder.root_path()) {
        rep.status = CleanStatus::Refused;
        return rep;
    }

    fs::path ign(rule.ignfile);
    if (ign.is_relative()) ign = folder / ign;

    // lstat: любой элемент с таким именем считается защитой.
    struct stat st;
    if (lstat(ign.c_str(), &st) == 0) {
        rep.status = CleanStatus::Ignored;
        return rep;
    }
    if (errno != ENOENT && errno != ENOTDIR) {
        rep.status = CleanStatus::Error;
        rep.message = "cannot check " + ign.string() + ": " + std::strerror(errno);
        return rep;
    }

    std::vector<fs::path> entries;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        entries.push_back(it->path());
    }
    if (ec) {
        rep.status = CleanStatus::Error;
        rep.message = "cannot read " + folder.string() + ": " + ec.message();
        return rep;
    }

    for (const fs::path &p : entries) {
        std::error_code rec;
        fs::remove_all(p, rec); 
        if (rec) {
            ++rep.failed;
            if (!rep.message.empty()) rep.message += "; ";
            rep.message += "cannot remove " + p.string() + ": " + rec.message();
        } else {
            ++rep.removed;
        }
    }
    rep.status = CleanStatus::Cleaned;
    return rep;
}

} 
