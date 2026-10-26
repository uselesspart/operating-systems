#include "treecleaner/cleaner.hpp"
#include "treecleaner/config.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class TempDirectory {
public:
    TempDirectory() {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() /
                ("treecleaner-test-" + std::to_string(static_cast<long long>(::getpid())) + "-" + std::to_string(nonce));
        fs::create_directories(path_);
    }
    ~TempDirectory() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    const fs::path& path() const { return path_; }
private:
    fs::path path_;
};

void createFile(const fs::path& path, const std::string& contents = "test") {
    fs::create_directories(path.parent_path());
    std::ofstream output(path);
    output << contents;
    require(static_cast<bool>(output), "не удалось создать тестовый файл " + path.string());
}

void testDepthOneRemovesImmediateDirectoriesAndPreservesRootFiles() {
    TempDirectory temp;
    const fs::path root = temp.path() / "root";
    fs::create_directories(root / "first" / "nested");
    fs::create_directories(root / "second");
    createFile(root / "first" / "nested" / "data.txt");
    createFile(root / "root.txt");

    const auto result = treecleaner::cleanAtDepth(root, 1);
    require(result.errors.empty(), "depth=1 не должен выдавать ошибки");
    require(result.removedDirectories.size() == 2, "depth=1 должен удалить две непосредственные папки");
    require(fs::exists(root), "исходный каталог должен сохраниться");
    require(fs::exists(root / "root.txt"), "файл в исходном каталоге должен сохраниться");
    require(!fs::exists(root / "first"), "первая непосредственная папка должна быть удалена");
    require(!fs::exists(root / "second"), "вторая непосредственная папка должна быть удалена");
}

void testDepthTwoPreservesFirstLevelDirectories() {
    TempDirectory temp;
    const fs::path root = temp.path() / "root";
    fs::create_directories(root / "branchA" / "removeA" / "deep");
    fs::create_directories(root / "branchB" / "removeB");
    createFile(root / "branchA" / "removeA" / "deep" / "file.txt");
    createFile(root / "branchA" / "keep.txt");
    createFile(root / "root.txt");

    const auto result = treecleaner::cleanAtDepth(root, 2);
    require(result.errors.empty(), "depth=2 не должен выдавать ошибки");
    require(result.removedDirectories.size() == 2, "depth=2 должен удалить две папки второго уровня");
    require(fs::exists(root / "branchA"), "каталог первого уровня должен сохраниться");
    require(fs::exists(root / "branchB"), "другой каталог первого уровня должен сохраниться");
    require(fs::exists(root / "branchA" / "keep.txt"), "файл вне удаляемой папки должен сохраниться");
    require(fs::exists(root / "root.txt"), "файл в исходном каталоге должен сохраниться");
    require(!fs::exists(root / "branchA" / "removeA"), "целевая папка второго уровня должна быть удалена");
    require(!fs::exists(root / "branchB" / "removeB"), "вторая целевая папка второго уровня должна быть удалена");
}

void testDepthBeyondTreeRemovesNothing() {
    TempDirectory temp;
    const fs::path root = temp.path() / "root";
    createFile(root / "one" / "two" / "three.txt");
    const auto result = treecleaner::cleanAtDepth(root, 8);
    require(result.errors.empty(), "слишком большая для дерева глубина должна быть допустимой");
    require(result.removedDirectories.empty(), "при отсутствии каталогов на заданной глубине ничего удалять не надо");
    require(fs::exists(root / "one" / "two" / "three.txt"), "дерево должно сохраниться");
}

void testSymlinkIsNotFollowed() {
    TempDirectory temp;
    const fs::path root = temp.path() / "root";
    const fs::path outside = temp.path() / "outside";
    createFile(root / "keep.txt");
    createFile(outside / "important.txt");
    fs::create_directory_symlink(outside, root / "external-link");

    const auto result = treecleaner::cleanAtDepth(root, 1);
    require(result.errors.empty(), "символическая ссылка должна просто игнорироваться");
    require(result.removedDirectories.empty(), "символическая ссылка не должна считаться каталогом");
    require(fs::is_symlink(fs::symlink_status(root / "external-link")), "символическая ссылка должна остаться на месте");
    require(fs::exists(outside / "important.txt"), "содержимое целевого каталога ссылки должно сохраниться");
}

void testSymlinkAsRootIsRejected() {
    TempDirectory temp;
    const fs::path realRoot = temp.path() / "real";
    createFile(realRoot / "sub" / "file.txt");
    const fs::path linkedRoot = temp.path() / "linked";
    fs::create_directory_symlink(realRoot, linkedRoot);

    const auto result = treecleaner::cleanAtDepth(fs::path(linkedRoot.string() + "/"), 1);
    require(!result.errors.empty(), "корневой путь-символическая ссылка должен быть отклонён даже с завершающим слэшем");
    require(fs::exists(realRoot / "sub" / "file.txt"), "содержимое исходной папки должно сохраниться");
}

void testConfigParsingAndRelativePaths() {
    TempDirectory temp;
    const fs::path configDir = temp.path() / "configuration";
    fs::create_directories(configDir);
    const fs::path configFile = configDir / "cleaner.conf";
    {
        std::ofstream output(configFile);
        output << "# comment\n\n../data 2 # trailing comment\n./logs 1\n";
    }
    const auto entries = treecleaner::loadConfig(configFile);
    require(entries.size() == 2, "пустые строки и комментарии должны игнорироваться");
    require(entries[0].folder == (configDir / "../data").lexically_normal(), "относительный путь должен считаться от каталога конфигурации");
    require(entries[0].depth == 2, "глубина первой записи должна быть прочитана");
    require(entries[1].folder == (configDir / "logs").lexically_normal(), "вторая запись должна быть прочитана");
}

void testInvalidConfigIsRejected() {
    TempDirectory temp;
    const fs::path configFile = temp.path() / "bad.conf";
    {
        std::ofstream output(configFile);
        output << "some-folder 0\n";
    }
    bool thrown = false;
    try {
        (void)treecleaner::loadConfig(configFile);
    } catch (const std::runtime_error&) {
        thrown = true;
    }
    require(thrown, "depth=0 должен считаться ошибкой конфигурации");
}

void testFilesystemRootIsRejected() {
    const auto result = treecleaner::cleanAtDepth(fs::path("/"), 1);
    require(!result.errors.empty(), "корень файловой системы должен быть отклонён");
    require(result.removedDirectories.empty(), "операция над корнем не должна ничего удалять");
}

void testDepthZeroNeverDeletesRoot() {
    TempDirectory temp;
    const fs::path root = temp.path() / "root";
    createFile(root / "sub" / "file.txt");
    const auto result = treecleaner::cleanAtDepth(root, 0);
    require(!result.errors.empty(), "depth=0 должен быть отклонён");
    require(fs::exists(root / "sub" / "file.txt"), "дерево не должно измениться при depth=0");
}

} // namespace

int main() {
    const struct TestCase { const char* name; void (*run)(); } tests[] = {
        {"depth one", testDepthOneRemovesImmediateDirectoriesAndPreservesRootFiles},
        {"depth two", testDepthTwoPreservesFirstLevelDirectories},
        {"depth beyond tree", testDepthBeyondTreeRemovesNothing},
        {"symlink child", testSymlinkIsNotFollowed},
        {"symlink root", testSymlinkAsRootIsRejected},
        {"config parsing", testConfigParsingAndRelativePaths},
        {"invalid config", testInvalidConfigIsRejected},
        {"depth zero", testDepthZeroNeverDeletesRoot},
        {"filesystem root protection", testFilesystemRootIsRejected},
    };

    std::size_t passed = 0;
    for (const auto& test : tests) {
        try {
            test.run();
            ++passed;
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& exception) {
            std::cerr << "[FAIL] " << test.name << ": " << exception.what() << '\n';
            return 1;
        }
    }
    std::cout << passed << " тестов пройдено\n";
    return 0;
}
