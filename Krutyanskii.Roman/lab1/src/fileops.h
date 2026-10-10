#ifndef LAB1_FILEOPS_H
#define LAB1_FILEOPS_H

#include <string>

namespace lab1 {
namespace fileops {

/// Returns true if 'path' exists (broken symlinks count as existing).
bool exists(const std::string& path);

/// Returns true if 'path' is a symbolic link. A broken symlink still counts.
bool isSymlink(const std::string& path);

/// Returns true if 'path' can be opened as a directory. A symlink to a
/// directory is followed, so a folder configured through a symlink is honoured.
bool isDirectory(const std::string& path);

/// Removes the content of the directory 'path', recursively, keeping the
/// directory itself. Entries inside are never followed: a symlink is unlinked
/// instead of being descended into. Returns false if the directory could not
/// be read or if any of its entries could not be removed.
bool clearDirectory(const std::string& path);

}  // namespace fileops
}  // namespace lab1

#endif  // LAB1_FILEOPS_H
