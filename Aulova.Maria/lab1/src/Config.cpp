
#include "Config.h"

#include <charconv>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace {

std::vector<std::string> splitFields(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    for (char c : line) {
        if (c == ' ' || c == '\t') {
            if (!current.empty()) {
                fields.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        fields.push_back(current);
    }
    return fields;
}

bool hasControlChars(const std::string& s) {
    for (char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c == 0x7F) {
            return true;
        }
    }
    return false;
}

bool isPlainFileName(const std::string& name) {
    return !name.empty() && name != "." && name != ".." &&
           name.find('/') == std::string::npos;
}

std::string trimTrailingSlashes(std::string s) {
    while (s.size() > 1 && s.back() == '/') {
        s.pop_back();
    }
    return s;
}

bool parseInterval(const std::string& token, int& value) {
    long long number = 0;
    const char* first = token.data();
    const char* last = first + token.size();
    const auto [ptr, ec] = std::from_chars(first, last, number);
    if (ec != std::errc() || ptr != last) {
        return false;
    }
    if (number < Config::kMinInterval || number > Config::kMaxInterval) {
        return false;
    }
    value = static_cast<int>(number);
    return true;
}

}

bool Config::load(const fs::path& file, Config& result, std::vector<std::string>& errors) {
    errors.clear();

    std::error_code ec;
    const fs::path path = fs::absolute(file, ec);
    if (ec) {
        errors.push_back("config file '" + file.string() + "': " + ec.message());
        return false;
    }

    const fs::file_status status = fs::status(path, ec);
    if (ec || !fs::is_regular_file(status)) {
        errors.push_back("config file '" + path.string() + "': " +
                         (ec ? ec.message() : std::string("not a regular file")));
        return false;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        errors.push_back("config file '" + path.string() + "': cannot open for reading");
        return false;
    }

    std::ostringstream content;
    content << in.rdbuf();
    if (in.bad()) {
        errors.push_back("config file '" + path.string() + "': read error");
        return false;
    }

    return parse(content.str(), path.parent_path(), result, errors);
}

bool Config::parse(const std::string& text, const fs::path& baseDir, Config& result,
                   std::vector<std::string>& errors) {
    errors.clear();

    std::error_code ec;
    const fs::path base = fs::absolute(baseDir, ec);
    if (ec) {
        errors.push_back("cannot resolve directory '" + baseDir.string() + "': " + ec.message());
        return false;
    }

    Config candidate;
    bool intervalSeen = false;
    std::set<fs::path> seenFolders;
    int lineNo = 0;
    std::string line;
    std::istringstream in(text);

    const auto fail = [&](const std::string& message) {
        errors.push_back("line " + std::to_string(lineNo) + ": " + message);
    };

    while (std::getline(in, line)) {
        ++lineNo;
        if (lineNo == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            line.erase(0, 3);
        }

        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        const std::vector<std::string> fields = splitFields(line);
        if (fields.empty() || fields[0][0] == '#') {
            continue;
        }

        if (fields[0] == "interval") {
            if (fields.size() != 2) {
                fail("expected 'interval N'");
                continue;
            }
            if (intervalSeen) {
                fail("duplicate 'interval' directive");
                continue;
            }
            intervalSeen = true;
            int value = 0;
            if (!parseInterval(fields[1], value)) {
                fail("invalid interval '" + fields[1] + "' (expected an integer from " +
                     std::to_string(kMinInterval) + " to " + std::to_string(kMaxInterval) + ")");
                continue;
            }
            candidate.interval_ = value;
            continue;
        }

        if (fields.size() != 2) {
            fail("expected 'folder ignfile', got " + std::to_string(fields.size()) + " fields");
            continue;
        }

        const std::string& folderToken = fields[0];
        const std::string& ignfile = fields[1];

        if (hasControlChars(folderToken) || hasControlChars(ignfile)) {
            fail("control characters are not allowed");
            continue;
        }
        if (!isPlainFileName(ignfile)) {
            fail("invalid ignfile '" + ignfile + "' (must be a plain file name without '/')");
            continue;
        }

        fs::path folder(trimTrailingSlashes(folderToken));
        if (folder.is_relative()) {
            folder = base / folder;
        }

        const fs::path real = fs::canonical(folder, ec);
        if (ec) {
            fail("folder '" + folderToken + "': " + ec.message());
            continue;
        }
        if (!fs::is_directory(real, ec)) {
            fail("'" + folderToken + "' is not a directory");
            continue;
        }
        if (!seenFolders.insert(real).second) {
            fail("duplicate folder '" + folderToken + "' (" + real.string() + ")");
            continue;
        }

        candidate.rules_.push_back(Rule{folder, ignfile});
    }

    if (errors.empty() && candidate.rules_.empty()) {
        errors.push_back("no folder rules defined");
    }
    if (!errors.empty()) {
        return false;
    }

    result = std::move(candidate);
    return true;
}
