#pragma once

#include <filesystem>

class Cleaner {
public:
    static void clean(
        const std::filesystem::path& folder,
        int max_depth
    );
};