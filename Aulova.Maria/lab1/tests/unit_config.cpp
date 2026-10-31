#include "Config.h"
#include "check.h"

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

using Errors = std::vector<std::string>;

bool hasError(const Errors& errors, const std::string& part) {
    for (const std::string& error : errors) {
        if (error.find(part) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool sameDir(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    return fs::equivalent(a, b, ec);
}

void testDefaultInterval() {
    TempDir tmp;
    tmp.mkdir("a");
    Config cfg;
    Errors errors;
    REQUIRE(Config::parse("a keep.me\n", tmp.path(), cfg, errors));
    CHECK(errors.empty());
    CHECK_EQ(cfg.interval(), 20);
    REQUIRE(cfg.rules().size() == 1);
    CHECK_EQ(cfg.rules()[0].ignfile, "keep.me");
    CHECK(cfg.rules()[0].folder.is_absolute());
    CHECK(sameDir(cfg.rules()[0].folder, tmp.path() / "a"));
}

void testIntervalValid() {
    TempDir tmp;
    tmp.mkdir("a");
    for (const char* value : {"1", "20", "3600", "86400"}) {
        Config cfg;
        Errors errors;
        const std::string text = std::string("interval ") + value + "\na keep\n";
        REQUIRE(Config::parse(text, tmp.path(), cfg, errors));
        CHECK_EQ(cfg.interval(), std::stoi(value));
    }

    Config cfg;
    Errors errors;
    REQUIRE(Config::parse("a keep\ninterval 2\n", tmp.path(), cfg, errors));
    CHECK_EQ(cfg.interval(), 2);
}

void testIntervalInvalid() {
    TempDir tmp;
    tmp.mkdir("a");
    for (const char* value : {"0", "-5", "abc", "86401", "100000", "1.5", "+5", "5s", "0x10",
                              "99999999999999999999"}) {
        Config cfg;
        Errors errors;
        const std::string text = std::string("interval ") + value + "\na keep\n";
        CHECK(!Config::parse(text, tmp.path(), cfg, errors));
        CHECK(errors.size() == 1);
        CHECK(hasError(errors, "invalid interval"));
    }

    for (const char* text : {"interval\na keep\n", "interval 5 6\na keep\n"}) {
        Config cfg;
        Errors errors;
        CHECK(!Config::parse(text, tmp.path(), cfg, errors));
        CHECK(hasError(errors, "expected 'interval N'"));
    }

    Config cfg;
    Errors errors;
    CHECK(!Config::parse("interval 5\ninterval 6\na keep\n", tmp.path(), cfg, errors));
    CHECK(hasError(errors, "duplicate 'interval'"));
}

void testMultipleRules() {
    TempDir tmp;
    tmp.mkdir("a");
    tmp.mkdir("b");
    tmp.mkdir("c");
    Config cfg;
    Errors errors;
    REQUIRE(Config::parse("a k1\nb k2\nc k3\n", tmp.path(), cfg, errors));
    REQUIRE(cfg.rules().size() == 3);
    CHECK(sameDir(cfg.rules()[0].folder, tmp.path() / "a"));
    CHECK(sameDir(cfg.rules()[1].folder, tmp.path() / "b"));
    CHECK(sameDir(cfg.rules()[2].folder, tmp.path() / "c"));
    CHECK_EQ(cfg.rules()[0].ignfile, "k1");
    CHECK_EQ(cfg.rules()[1].ignfile, "k2");
    CHECK_EQ(cfg.rules()[2].ignfile, "k3");
}

void testRelativeAndAbsolutePaths() {
    TempDir tmp;
    tmp.mkdir("conf/inner");
    tmp.mkdir("data");
    tmp.mkdir("other");
    const std::string text = "../data keep.me\n" + (tmp.path() / "other").string() +
                             " .protect\n" + "inner marker\n";
    Config cfg;
    Errors errors;
    REQUIRE(Config::parse(text, tmp.path() / "conf", cfg, errors));
    REQUIRE(cfg.rules().size() == 3);
    CHECK(sameDir(cfg.rules()[0].folder, tmp.path() / "data"));
    CHECK(sameDir(cfg.rules()[1].folder, tmp.path() / "other"));
    CHECK(sameDir(cfg.rules()[2].folder, tmp.path() / "conf" / "inner"));
    for (const Config::Rule& rule : cfg.rules()) {
        CHECK(rule.folder.is_absolute());
    }
}

void testCommentsBlankLinesCrlfBom() {
    TempDir tmp;
    tmp.mkdir("a");
    tmp.mkdir("b");
    const std::string text = std::string("\xEF\xBB\xBF") +
                             "# comment\r\n\r\n   \t\r\ninterval 3\r\n"
                             "  a \t keep.me \r\n   # indented comment\r\nb  x\r\n";
    Config cfg;
    Errors errors;
    REQUIRE(Config::parse(text, tmp.path(), cfg, errors));
    CHECK_EQ(cfg.interval(), 3);
    REQUIRE(cfg.rules().size() == 2);
    CHECK_EQ(cfg.rules()[0].ignfile, "keep.me");
    CHECK_EQ(cfg.rules()[1].ignfile, "x");
}

void testWrongFieldCount() {
    TempDir tmp;
    tmp.mkdir("a");
    for (const char* text : {"a\n", "a keep extra\n", "a keep # note\n"}) {
        Config cfg;
        Errors errors;
        CHECK(!Config::parse(text, tmp.path(), cfg, errors));
        CHECK(hasError(errors, "line 1"));
        CHECK(hasError(errors, "expected 'folder ignfile'"));
    }

    Config cfg;
    Errors errors;
    CHECK(!Config::parse("interval 5\n\nbad\n", tmp.path(), cfg, errors));
    CHECK(hasError(errors, "line 3"));
}

void testIgnfileMustBePlainName() {
    TempDir tmp;
    tmp.mkdir("a");
    for (const char* name : {"sub/keep", "/abs", ".", "..", "../x", "dir/"}) {
        Config cfg;
        Errors errors;
        const std::string text = std::string("a ") + name + "\n";
        CHECK(!Config::parse(text, tmp.path(), cfg, errors));
        CHECK(hasError(errors, "invalid ignfile"));
    }
}

void testFolderMustBeDirectory() {
    TempDir tmp;
    tmp.write("file.txt", "x");
    Config cfg;
    Errors errors;
    CHECK(!Config::parse("missing keep\n", tmp.path(), cfg, errors));
    CHECK(hasError(errors, "missing"));
    CHECK(!Config::parse("file.txt keep\n", tmp.path(), cfg, errors));
    CHECK(hasError(errors, "not a directory"));
}

void testDuplicateFolders() {
    TempDir tmp;
    tmp.mkdir("a");
    fs::create_directory_symlink(tmp.path() / "a", tmp.path() / "link");
    Config cfg;
    Errors errors;
    CHECK(!Config::parse("a k\na k2\n", tmp.path(), cfg, errors));
    CHECK(hasError(errors, "duplicate folder"));
    CHECK(!Config::parse("a k\n./a/ k\n", tmp.path(), cfg, errors));
    CHECK(hasError(errors, "duplicate folder"));
    CHECK(!Config::parse("a k\nlink k\n", tmp.path(), cfg, errors));
    CHECK(hasError(errors, "duplicate folder"));
}

void testSymlinkFolderAndTrailingSlash() {
    TempDir tmp;
    tmp.mkdir("a");
    fs::create_directory_symlink(tmp.path() / "a", tmp.path() / "link");
    Config cfg;
    Errors errors;
    REQUIRE(Config::parse("link keep\n", tmp.path(), cfg, errors));
    REQUIRE(cfg.rules().size() == 1);
    CHECK(cfg.rules()[0].folder == tmp.path() / "link");

    REQUIRE(Config::parse("a/// keep\n", tmp.path(), cfg, errors));
    REQUIRE(cfg.rules().size() == 1);
    CHECK(cfg.rules()[0].folder == tmp.path() / "a");
}

void testEmptyConfigRejected() {
    TempDir tmp;
    for (const char* text : {"", "\n\n", "# only comment\n", "interval 5\n"}) {
        Config cfg;
        Errors errors;
        CHECK(!Config::parse(text, tmp.path(), cfg, errors));
        CHECK(hasError(errors, "no folder rules"));
    }
}

void testControlCharactersRejected() {
    TempDir tmp;
    tmp.mkdir("a");
    const std::string texts[] = {
        std::string("a ke") + '\x01' + "ep\n",
        std::string("a ke") + '\0' + "ep\n",
        std::string("a") + '\x0b' + " keep\n",
    };
    for (const std::string& text : texts) {
        Config cfg;
        Errors errors;
        CHECK(!Config::parse(text, tmp.path(), cfg, errors));
        CHECK(hasError(errors, "control characters"));
    }
}

void testFailureKeepsPreviousConfig() {
    TempDir tmp;
    tmp.mkdir("a");
    tmp.mkdir("b");
    Config cfg;
    Errors errors;
    REQUIRE(Config::parse("interval 7\na keep1\n", tmp.path(), cfg, errors));

    CHECK(!Config::parse("interval 9\nb keep2\nmissing keep\n", tmp.path(), cfg, errors));
    CHECK_EQ(cfg.interval(), 7);
    REQUIRE(cfg.rules().size() == 1);
    CHECK_EQ(cfg.rules()[0].ignfile, "keep1");
    CHECK(sameDir(cfg.rules()[0].folder, tmp.path() / "a"));
}

void testAllErrorsReported() {
    TempDir tmp;
    Config cfg;
    Errors errors;
    CHECK(!Config::parse("bad\nmissing keep\ninterval x\n", tmp.path(), cfg, errors));
    CHECK(errors.size() == 3);
    CHECK(hasError(errors, "line 1"));
    CHECK(hasError(errors, "line 2"));
    CHECK(hasError(errors, "line 3"));
}

void testLoadReadsFile() {
    TempDir tmp;
    tmp.mkdir("conf");
    tmp.mkdir("data");
    const fs::path cfgPath = tmp.write("conf/app.conf", "interval 4\n../data keep\n");
    Config cfg;
    Errors errors;
    REQUIRE(Config::load(cfgPath, cfg, errors));
    CHECK_EQ(cfg.interval(), 4);
    REQUIRE(cfg.rules().size() == 1);
    CHECK(sameDir(cfg.rules()[0].folder, tmp.path() / "data"));
}

void testLoadRelativeConfigPath() {
    TempDir tmp;
    tmp.mkdir("data");
    tmp.write("app.conf", "data keep\n");
    const fs::path oldCwd = fs::current_path();
    fs::current_path(tmp.path());
    Config cfg;
    Errors errors;
    const bool ok = Config::load("app.conf", cfg, errors);
    fs::current_path(oldCwd);
    REQUIRE(ok);
    REQUIRE(cfg.rules().size() == 1);
    CHECK(cfg.rules()[0].folder.is_absolute());
    CHECK(sameDir(cfg.rules()[0].folder, tmp.path() / "data"));
}

void testLoadErrors() {
    TempDir tmp;
    tmp.mkdir("dir");
    Config cfg;
    Errors errors;
    CHECK(!Config::load(tmp.path() / "missing.conf", cfg, errors));
    CHECK(errors.size() == 1);
    CHECK(hasError(errors, "config file"));
    CHECK(!Config::load(tmp.path() / "dir", cfg, errors));
    CHECK(hasError(errors, "not a regular file"));
}

}  // namespace

int main() {
    testDefaultInterval();
    testIntervalValid();
    testIntervalInvalid();
    testMultipleRules();
    testRelativeAndAbsolutePaths();
    testCommentsBlankLinesCrlfBom();
    testWrongFieldCount();
    testIgnfileMustBePlainName();
    testFolderMustBeDirectory();
    testDuplicateFolders();
    testSymlinkFolderAndTrailingSlash();
    testEmptyConfigRejected();
    testControlCharactersRejected();
    testFailureKeepsPreviousConfig();
    testAllErrorsReported();
    testLoadReadsFile();
    testLoadRelativeConfigPath();
    testLoadErrors();
    return check::summary("unit_config");
}