#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include <sys/types.h>

struct FileEvent {
    int wd;
    std::uint32_t mask;
    std::uint32_t cookie;
    std::string name;
};

std::vector<FileEvent> parse_events(const char* buffer, std::size_t size);
std::string format_event(const std::filesystem::path& path, const FileEvent& event);

class FileWatcher {
public:
    explicit FileWatcher(const std::vector<std::filesystem::path>& paths);
    ~FileWatcher();

    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    void wait(int timeout_ms, bool (*stop)() = nullptr);
    void drain(bool (*stop)() = nullptr);
    bool update(const std::vector<std::filesystem::path>& paths, bool (*stop)() = nullptr);
    void swap(FileWatcher& other) noexcept;
    void process_events(const std::vector<FileEvent>& events, bool (*stop)() = nullptr);

private:
    struct Watch {
        std::filesystem::path path;
        dev_t parent_device;
        ino_t parent_inode;
        bool active = true;
    };

    static Watch make_watch(const std::filesystem::path& path);
    void reset(bool strict);
    void read_events(bool (*stop)() = nullptr);
    void remove_watch(int wd, Watch& watch);
    void remove_directory(const std::filesystem::path& path);

    int fd = -1;
    std::vector<std::filesystem::path> paths;
    std::map<int, Watch> watches;
};
