#include "SafeCleaner.h"

#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kMaxMessages = 20;

struct Probe {
    bool ok = false;
    int err = 0;
    dev_t dev = 0;
    ino_t ino = 0;
    bool isDir = false;

    bool absent() const { return !ok && err == ENOENT; }

    bool sameAs(const Probe& other) const {
        return ok && other.ok && dev == other.dev && ino == other.ino;
    }
};

Probe probe(const fs::path& path) {
    Probe result;
    struct stat st;
    if (::lstat(path.c_str(), &st) != 0) {
        result.err = errno;
        return result;
    }
    result.ok = true;
    result.dev = st.st_dev;
    result.ino = st.st_ino;
    result.isDir = S_ISDIR(st.st_mode);
    return result;
}

std::string errnoText(int err) {
    return std::generic_category().message(err);
}

bool isPlainName(const std::string& name) {
    return !name.empty() && name != "." && name != ".." &&
           name.find('/') == std::string::npos && name.find('\0') == std::string::npos;
}

fs::path normalized(const fs::path& path) {
    std::error_code ec;
    fs::path result = fs::absolute(path, ec);
    if (ec) {
        result = path;
    }
    result = result.lexically_normal();
    if (result.has_relative_path() && result.filename().empty()) {
        result = result.parent_path();
    }
    return result;
}

bool isSameOrUnder(const fs::path& path, const fs::path& root) {
    const auto mismatch = std::mismatch(root.begin(), root.end(), path.begin(), path.end());
    return mismatch.first == root.end();
}

bool listNames(const fs::path& dir, std::vector<fs::path>& names, std::string& error) {
    std::error_code ec;
    fs::directory_iterator it(dir, ec);
    const fs::directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        names.push_back(it->path().filename());
    }
    if (ec) {
        error = ec.message();
        return false;
    }
    return true;
}

void addVariants(std::vector<fs::path>& list, const fs::path& path) {
    std::error_code ec;
    const fs::path given = fs::absolute(path, ec);
    if (ec || given.empty()) {
        return;
    }
    list.push_back(given);
    const fs::path parent = fs::weakly_canonical(given.parent_path(), ec);
    if (!ec) {
        list.push_back(parent / given.filename());
    }
    ec.clear();
    const fs::path full = fs::weakly_canonical(given, ec);
    if (!ec) {
        list.push_back(full);
    }
}

std::string withoutDeletedSuffix(std::string text) {
    const std::string suffix = " (deleted)";
    if (text.size() > suffix.size() &&
        text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0) {
        text.erase(text.size() - suffix.size());
    }
    return text;
}

bool isOctal(char c) {
    return c >= '0' && c <= '7';
}

std::string unescapeMount(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 3 < text.size() && isOctal(text[i + 1]) &&
            isOctal(text[i + 2]) && isOctal(text[i + 3])) {
            const int value =
                (text[i + 1] - '0') * 64 + (text[i + 2] - '0') * 8 + (text[i + 3] - '0');
            out += static_cast<char>(value & 0xFF);
            i += 3;
        } else {
            out += text[i];
        }
    }
    return out;
}

std::string readText(const fs::path& path) {
    std::ifstream in(path);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

class Remover {
public:
    Remover(const fs::path& root, const Probe& rootProbe, const std::string& ignfile,
            const SafeCleaner::Hook& hook, SafeCleaner::Report& report)
        : root_(root), rootProbe_(rootProbe), ignfile_(ignfile), hook_(hook), report_(report) {}

    void run() {
        emptyDirectory(root_, rootProbe_);
        if (suppressed_ > 0) {
            report_.messages.push_back(std::to_string(suppressed_) +
                                       " more messages suppressed");
        }
        const std::size_t removed = report_.removedFiles + report_.removedDirs;
        if (aborted_ && removed > 0) {
            report_.outcome = SafeCleaner::Outcome::Interrupted;
            report_.messages.push_back("cleanup stopped after removing " +
                                       std::to_string(report_.removedFiles) + " files and " +
                                       std::to_string(report_.removedDirs) + " directories");
        } else if (protectedAppeared_) {
            report_.outcome = SafeCleaner::Outcome::Protected;
        } else if (failed_) {
            report_.outcome = SafeCleaner::Outcome::Failed;
        } else {
            report_.outcome = SafeCleaner::Outcome::Cleaned;
        }
    }

private:
    enum class Result { Removed, Gone, Failed };

    void note(const std::string& message) {
        if (report_.messages.size() < kMaxMessages) {
            report_.messages.push_back(message);
        } else {
            ++suppressed_;
        }
    }

    void fail(const std::string& message) {
        failed_ = true;
        note(message);
    }

    void stop(const std::string& message) {
        aborted_ = true;
        fail(message);
    }

    bool mayRemove(const fs::path& dir, const Probe& dirProbe, const fs::path& target) {
        if (hook_) {
            hook_(target);
        }
        const Probe now = probe(dir);
        if (!now.isDir || !now.sameAs(dirProbe)) {
            stop("directory changed during cleanup: '" + dir.string() + "'");
            return false;
        }
        const Probe ign = probe(root_ / ignfile_);
        if (ign.ok) {
            protectedAppeared_ = true;
            aborted_ = true;
            note("protective file '" + ignfile_ + "' appeared during cleanup");
            return false;
        }
        if (!ign.absent()) {
            stop("cannot verify protective file '" + ignfile_ +
                 "' during cleanup: " + errnoText(ign.err));
            return false;
        }
        return true;
    }

    bool emptyDirectory(const fs::path& dir, const Probe& dirProbe) {
        std::vector<fs::path> names;
        std::string error;
        if (!listNames(dir, names, error)) {
            fail("cannot list '" + dir.string() + "': " + error);
            return false;
        }
        bool allGone = true;
        for (const fs::path& name : names) {
            if (aborted_) {
                return false;
            }
            const Result result = removeEntry(dir, dirProbe, name);
            if (result == Result::Failed) {
                allGone = false;
            } else if (result == Result::Removed && dir == root_) {
                report_.removedTopLevel.push_back(name.string());
            }
        }
        return allGone && !aborted_;
    }

    Result removeEntry(const fs::path& dir, const Probe& dirProbe, const fs::path& name) {
        const fs::path path = dir / name;
        const Probe info = probe(path);
        if (!info.ok) {
            if (info.absent()) {
                return Result::Gone;
            }
            fail("cannot inspect '" + path.string() + "': " + errnoText(info.err));
            return Result::Failed;
        }
        if (info.dev != rootProbe_.dev) {
            fail("'" + path.string() + "' is on another filesystem, skipped");
            return Result::Failed;
        }
        if (info.isDir && !emptyDirectory(path, info)) {
            return Result::Failed;
        }
        if (!mayRemove(dir, dirProbe, path)) {
            return Result::Failed;
        }
        std::error_code ec;
        const bool removed = fs::remove(path, ec);
        if (ec) {
            fail("cannot remove '" + path.string() + "': " + ec.message());
            return Result::Failed;
        }
        if (!removed) {
            return Result::Gone;
        }
        if (info.isDir) {
            ++report_.removedDirs;
        } else {
            ++report_.removedFiles;
        }
        return Result::Removed;
    }

    fs::path root_;
    Probe rootProbe_;
    std::string ignfile_;
    const SafeCleaner::Hook& hook_;
    SafeCleaner::Report& report_;
    bool failed_ = false;
    bool aborted_ = false;
    bool protectedAppeared_ = false;
    std::size_t suppressed_ = 0;
};

}  // namespace

std::vector<fs::path> SafeCleaner::Policy::windowsDrives(const std::string& mountsText) {
    std::vector<fs::path> drives;
    std::istringstream lines(mountsText);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        std::string device;
        std::string mountPoint;
        std::string type;
        std::string options;
        if (!(fields >> device >> mountPoint >> type >> options)) {
            continue;
        }
        const bool windows = type == "drvfs" || (type == "9p" && options.find("aname=drvfs") !=
                                                                     std::string::npos);
        const fs::path path(unescapeMount(mountPoint));
        if (windows && path.is_absolute() && path != fs::path("/")) {
            drives.push_back(path);
        }
    }
    return drives;
}

SafeCleaner::Policy SafeCleaner::Policy::standard(const fs::path& configFile,
                                                  const fs::path& pidFile) {
    Policy policy;

    for (const char* dir : {"/bin", "/boot", "/dev", "/etc", "/lib", "/lib32", "/lib64",
                            "/libx32", "/proc", "/sbin", "/sys", "/usr", "/snap",
                            "/lost+found", "/var/lib", "/var/log", "/mnt/wsl", "/mnt/wslg"}) {
        addVariants(policy.protectedTrees, dir);
    }

    for (const char* dir : {"/", "/home", "/root", "/var", "/var/tmp", "/run", "/tmp", "/opt",
                            "/srv", "/mnt", "/media"}) {
        addVariants(policy.forbiddenExact, dir);
    }

    for (char letter = 'a'; letter <= 'z'; ++letter) {
        addVariants(policy.protectedTrees, fs::path("/mnt") / std::string(1, letter));
    }
    for (const fs::path& drive : windowsDrives(readText("/proc/self/mounts"))) {
        addVariants(policy.protectedTrees, drive);
    }

    std::vector<fs::path> homes;
    const char* homeEnv = std::getenv("HOME");
    if (homeEnv != nullptr && *homeEnv != '\0') {
        homes.emplace_back(homeEnv);
    }
    const passwd* entry = getpwuid(getuid());
    if (entry != nullptr && entry->pw_dir != nullptr && *entry->pw_dir != '\0') {
        homes.emplace_back(entry->pw_dir);
    }
    for (const fs::path& home : homes) {
        addVariants(policy.protectedPaths, home);
        for (const char* sub : {".ssh", ".gnupg", ".config", ".local"}) {
            addVariants(policy.protectedTrees, home / sub);
        }
    }

    addVariants(policy.protectedPaths, configFile);
    addVariants(policy.protectedPaths, pidFile);

    std::error_code ec;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        addVariants(policy.protectedPaths, withoutDeletedSuffix(exe.string()));
    }

    return policy;
}

SafeCleaner::SafeCleaner(Policy policy) : policy_(std::move(policy)) {
    for (std::vector<fs::path>* list :
         {&policy_.protectedTrees, &policy_.forbiddenExact, &policy_.protectedPaths}) {
        for (fs::path& path : *list) {
            path = normalized(path);
        }
        list->erase(std::remove_if(list->begin(), list->end(),
                                   [](const fs::path& p) { return p.empty(); }),
                    list->end());
    }
}

void SafeCleaner::setBeforeRemoveHook(Hook hook) {
    hook_ = std::move(hook);
}

bool SafeCleaner::checkFolder(const fs::path& folder, fs::path& resolved,
                              std::string& reason) const {
    if (!folder.is_absolute()) {
        reason = "path is not absolute";
        return false;
    }

    std::error_code ec;
    const fs::path real = fs::canonical(folder, ec);
    if (ec) {
        reason = "cannot resolve path: " + ec.message();
        return false;
    }
    if (!fs::is_directory(real, ec)) {
        reason = "not a directory";
        return false;
    }

    const fs::path path = normalized(real);

    for (const fs::path& root : policy_.protectedTrees) {
        if (isSameOrUnder(path, root)) {
            reason = "is or is inside protected system directory '" + root.string() + "'";
            return false;
        }
        if (isSameOrUnder(root, path)) {
            reason = "contains protected system directory '" + root.string() + "'";
            return false;
        }
    }
    for (const fs::path& exact : policy_.forbiddenExact) {
        if (path == exact) {
            reason = "directory '" + exact.string() + "' must not be cleaned";
            return false;
        }
    }
    for (const fs::path& keep : policy_.protectedPaths) {
        if (isSameOrUnder(keep, path)) {
            reason = "is or contains protected path '" + keep.string() + "'";
            return false;
        }
    }

    resolved = path;
    return true;
}

std::vector<std::string> SafeCleaner::assess(const std::vector<Config::Rule>& rules,
                                             std::vector<fs::path>& resolved) const {
    std::vector<std::string> reasons(rules.size());
    resolved.assign(rules.size(), fs::path());

    for (std::size_t i = 0; i < rules.size(); ++i) {
        if (!isPlainName(rules[i].ignfile)) {
            reasons[i] = "invalid protective file name '" + rules[i].ignfile + "'";
            continue;
        }
        std::string reason;
        if (!checkFolder(rules[i].folder, resolved[i], reason)) {
            reasons[i] = std::move(reason);
        }
    }

    const std::vector<std::string> base = reasons;
    for (std::size_t i = 0; i < rules.size(); ++i) {
        for (std::size_t j = i + 1; j < rules.size(); ++j) {
            if (!base[i].empty() || !base[j].empty()) {
                continue;
            }
            if (isSameOrUnder(resolved[i], resolved[j]) ||
                isSameOrUnder(resolved[j], resolved[i])) {
                if (reasons[i].empty()) {
                    reasons[i] = "overlaps with '" + resolved[j].string() + "'";
                }
                if (reasons[j].empty()) {
                    reasons[j] = "overlaps with '" + resolved[i].string() + "'";
                }
            }
        }
    }
    return reasons;
}

std::vector<std::string> SafeCleaner::validate(const std::vector<Config::Rule>& rules) const {
    std::vector<fs::path> resolved;
    const std::vector<std::string> reasons = assess(rules, resolved);
    std::vector<std::string> errors;
    for (std::size_t i = 0; i < rules.size(); ++i) {
        if (!reasons[i].empty()) {
            errors.push_back("folder '" + rules[i].folder.string() + "': " + reasons[i]);
        }
    }
    return errors;
}

SafeCleaner::Report SafeCleaner::cleanResolved(const Config::Rule& rule,
                                               const fs::path& resolved) const {
    Report report;
    report.folder = resolved;
    report.outcome = Outcome::Failed;

    const Probe root = probe(resolved);
    if (!root.isDir) {
        report.messages.push_back("cannot inspect folder: " +
                                  (root.ok ? std::string("not a directory")
                                           : errnoText(root.err)));
        return report;
    }

    const fs::path ignPath = resolved / rule.ignfile;
    const Probe ign = probe(ignPath);
    if (ign.ok) {
        report.outcome = Outcome::Protected;
        report.messages.push_back("protective file '" + rule.ignfile +
                                  "' exists, nothing removed");
        return report;
    }
    if (!ign.absent()) {
        report.messages.push_back("cannot verify protective file '" + ignPath.string() +
                                  "': " + errnoText(ign.err) + ", nothing removed");
        return report;
    }

    fs::path again;
    std::string reason;
    if (!checkFolder(rule.folder, again, reason) || again != resolved ||
        !probe(resolved).sameAs(root)) {
        report.messages.push_back("folder changed during check, nothing removed" +
                                  (reason.empty() ? std::string() : ": " + reason));
        return report;
    }

    Remover remover(resolved, root, rule.ignfile, hook_, report);
    remover.run();
    return report;
}

std::vector<SafeCleaner::Report> SafeCleaner::cleanAll(
    const std::vector<Config::Rule>& rules) const {
    std::vector<fs::path> resolved;
    const std::vector<std::string> rejected = assess(rules, resolved);

    std::vector<Report> reports(rules.size());
    for (std::size_t i = 0; i < rules.size(); ++i) {
        if (!rejected[i].empty()) {
            reports[i].folder = rules[i].folder;
            reports[i].outcome = Outcome::Rejected;
            reports[i].messages.push_back(rejected[i]);
        } else {
            reports[i] = cleanResolved(rules[i], resolved[i]);
        }
    }
    return reports;
}

SafeCleaner::Report SafeCleaner::clean(const Config::Rule& rule) const {
    return cleanAll(std::vector<Config::Rule>{rule}).front();
}