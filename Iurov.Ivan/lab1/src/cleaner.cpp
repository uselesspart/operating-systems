#include "treecleaner/cleaner.hpp"

#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace treecleaner {
namespace {

fs::path stripTrailingSlashes(const fs::path& input) {
    std::string value = input.lexically_normal().string();
    while (value.size() > 1 && value.back() == '/') {
        value.pop_back();
    }
    return fs::path(value);
}

void collectDirectoriesAtDepth(const fs::path& current,
                               const std::size_t currentDepth,
                               const std::size_t targetDepth,
                               std::vector<fs::path>& matches,
                               std::vector<std::string>& errors) {
    std::error_code ec;
    fs::directory_iterator iterator(current, fs::directory_options::none, ec);
    if (ec) {
        errors.push_back("не удалось прочитать каталог '" + current.string() + "': " + ec.message());
        return;
    }

    const fs::directory_iterator end;
    while (iterator != end) {
        const fs::directory_entry entry = *iterator;
        const fs::path entryPath = entry.path();
        std::error_code statusError;
        const fs::file_status status = entry.symlink_status(statusError);
        if (statusError) {
            errors.push_back("не удалось проверить '" + entryPath.string() + "': " + statusError.message());
        } else if (fs::is_directory(status)) {
            const std::size_t nextDepth = currentDepth + 1;
            if (nextDepth == targetDepth) {
                matches.push_back(entryPath);
            } else if (nextDepth < targetDepth) {
                collectDirectoriesAtDepth(entryPath, nextDepth, targetDepth, matches, errors);
            }
        }

        std::error_code incrementError;
        iterator.increment(incrementError);
        if (incrementError) {
            errors.push_back("ошибка обхода каталога '" + current.string() + "': " + incrementError.message());
            break;
        }
    }
}

} // namespace

CleanResult cleanAtDepth(const fs::path& requestedRoot, const std::size_t depth) {
    CleanResult result;
    if (depth == 0) {
        result.errors.push_back("глубина 0 запрещена: исходный каталог не должен удаляться");
        return result;
    }

    const fs::path checkedRoot = stripTrailingSlashes(requestedRoot);
    std::error_code ec;
    const fs::file_status rootLinkStatus = fs::symlink_status(checkedRoot, ec);
    if (ec) {
        result.errors.push_back("не удалось проверить исходный каталог '" + checkedRoot.string() + "': " + ec.message());
        return result;
    }
    if (fs::is_symlink(rootLinkStatus)) {
        result.errors.push_back("исходный путь является символической ссылкой; операция запрещена: '" +
                                checkedRoot.string() + "'");
        return result;
    }
    if (!fs::is_directory(rootLinkStatus)) {
        result.errors.push_back("исходный путь не является каталогом: '" + checkedRoot.string() + "'");
        return result;
    }

    const fs::path root = fs::weakly_canonical(checkedRoot, ec);
    if (ec) {
        result.errors.push_back("не удалось канонизировать исходный каталог '" + checkedRoot.string() + "': " + ec.message());
        return result;
    }
    if (root == root.root_path()) {
        result.errors.push_back("удаление подкаталогов корня файловой системы запрещено: '" + root.string() + "'");
        return result;
    }

    std::vector<fs::path> targets;
    collectDirectoriesAtDepth(root, 0, depth, targets, result.errors);

    for (const fs::path& target : targets) {
        // Recheck immediately before deletion. Symlinks are not traversed and
        // if a directory was replaced with a symlink, that link is left alone.
        std::error_code statusError;
        const fs::file_status status = fs::symlink_status(target, statusError);
        if (statusError) {
            result.errors.push_back("не удалось повторно проверить каталог '" + target.string() + "': " + statusError.message());
            continue;
        }
        if (!fs::is_directory(status) || fs::is_symlink(status)) {
            result.errors.push_back("цель изменилась во время обхода, пропущено: '" + target.string() + "'");
            continue;
        }

        std::error_code removeError;
        fs::remove_all(target, removeError);
        if (removeError) {
            result.errors.push_back("не удалось удалить каталог '" + target.string() + "': " + removeError.message());
        } else {
            result.removedDirectories.push_back(target);
        }
    }

    return result;
}

} // namespace treecleaner
