#pragma once

#include <filesystem>

namespace dirclean {

class PidManager {
public:
    explicit PidManager(std::filesystem::path pidFile);

    long read() const;
    void write(long pid) const;
    void remove() const;

    static bool processAlive(long pid);

    // Останавливает процесс из pid-файла, если он жив; возвращает его pid или 0.
    long stopPrevious(long timeoutMs = 5000) const;

    const std::filesystem::path& path() const { return pidFile_; }

private:
    std::filesystem::path pidFile_;
};

}
