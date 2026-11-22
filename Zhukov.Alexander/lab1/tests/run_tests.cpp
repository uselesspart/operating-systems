#include "file_watcher.h"
#include "logger.h"
#include "fd.h"
#include "disk_monitor.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/file.h>
#include <syslog.h>
#include <unistd.h>

static_assert(std::is_base_of_v<Daemon, DiskMonitor>);
static_assert(std::is_abstract_v<Daemon>);
static_assert(!std::is_copy_constructible_v<DiskMonitor>);
static_assert(!std::is_move_constructible_v<DiskMonitor>);
static_assert(!std::is_default_constructible_v<DiskMonitor>);

namespace fs = std::filesystem;
using namespace std::chrono_literals;

fs::path binary;
fs::path sanitizer_dir;
std::vector<fs::path> checked_reports;

std::string show_text(const std::string& text) {
    std::string result = "\"";
    for (char c : text) {
        switch (c) {
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            default: result += c;
        }
    }
    return result + '"';
}

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message.empty() ? "Check failed" : message);
    }
}

void expect_contains(const std::string& text, const std::string& value) {
    expect(text.find(value) != std::string::npos,
           "Ожидалась строка: " + show_text(value) + "\nПолучено: " + show_text(text));
}

void expect_absent(const std::string& text, const std::string& value) {
    expect(text.find(value) == std::string::npos,
           "Строка должна отсутствовать: " + show_text(value) + "\nПолучено: " + show_text(text));
}

void expect_equal(const std::string& actual, const std::string& expected) {
    expect(actual == expected,
           "Ожидалось: " + show_text(expected) + "\nПолучено: " + show_text(actual));
}

class TempDir {
public:
    fs::path path;

    TempDir() {
        auto name = (fs::temp_directory_path() / "os-lab1-test-XXXXXX").string();
        if (mkdtemp(name.data()) == nullptr) {
            throw std::system_error(errno, std::generic_category(), "mkdtemp failed");
        }
        path = name;
    }

    ~TempDir() {
        std::error_code error;
        fs::remove_all(path, error);
        if (error) {
            std::cerr << "Cannot remove: " << path << '\n';
        }
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

class Permissions {
public:
    Permissions(const fs::path& file, fs::perms mode)
        : path(file), saved(fs::status(file).permissions()) {
        fs::permissions(path, mode);
    }

    ~Permissions() {
        std::error_code error;
        fs::permissions(path, saved, error);
        if (error) {
            std::cerr << "Cannot restore permissions: " << path << '\n';
        }
    }

    Permissions(const Permissions&) = delete;
    Permissions& operator=(const Permissions&) = delete;

private:
    fs::path path;
    fs::perms saved;
};

void write_file(const fs::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    file << text;
    file.close();
    expect(!file.fail(), "Cannot write: " + path.string());
}

std::string read_file(const fs::path& path, std::uintmax_t offset = 0) {
    std::ifstream file(path, std::ios::binary);
    expect(file.is_open(), "Cannot read: " + path.string());
    file.seekg(fs::file_size(path) >= offset ? offset : 0);
    expect(!file.fail(), "Cannot seek: " + path.string());
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    expect(!file.bad(), "Cannot read: " + path.string());
    return text;
}

void check_sanitizer_reports() {
    if (sanitizer_dir.empty()) {
        return;
    }
    std::string errors;
    for (const auto& entry : fs::directory_iterator(sanitizer_dir)) {
        if (std::find(checked_reports.begin(), checked_reports.end(), entry.path()) != checked_reports.end()) {
            continue;
        }
        const auto text = read_file(entry.path());
        if (!text.empty()) {
            checked_reports.push_back(entry.path());
            errors += entry.path().filename().string() + ":\n" + text + '\n';
        }
    }
    expect(errors.empty(), "ASan/UBSan:\n" + errors);
}

struct Result {
    int code;
    std::string out;
    std::string err;
};

void expect_code(const Result& result, int expected) {
    expect(result.code == expected,
           "Код завершения: ожидался " + std::to_string(expected) +
           ", получен " + std::to_string(result.code) +
           "\nstdout: " + show_text(result.out) + "\nstderr: " + show_text(result.err));
}

Result run(const std::vector<std::string>& args, const fs::path& cwd = "/",
           std::chrono::seconds timeout = 5s, bool close_input = false) {
    int output[2];
    expect(pipe2(output, O_CLOEXEC | O_NONBLOCK) == 0, "Cannot create stdout pipe");
    Fd out_reader(output[0]);
    Fd out_writer(output[1]);
    int errors[2];
    expect(pipe2(errors, O_CLOEXEC | O_NONBLOCK) == 0, "Cannot create stderr pipe");
    Fd err_reader(errors[0]);
    Fd err_writer(errors[1]);
    std::vector<char*> argv;
    for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) {
        throw std::system_error(errno, std::generic_category(), "fork failed");
    }
    if (pid == 0) {
        if (setsid() < 0 || chdir(cwd.c_str()) != 0 ||
            fcntl(out_writer.get(), F_SETFL, 0) < 0 || fcntl(err_writer.get(), F_SETFL, 0) < 0 ||
            dup2(out_writer.get(), STDOUT_FILENO) < 0 || dup2(err_writer.get(), STDERR_FILENO) < 0) {
            _exit(127);
        }
        out_reader.reset();
        out_writer.reset();
        err_reader.reset();
        err_writer.reset();
        if (close_input) {
            close(STDIN_FILENO);
        }
        if (!sanitizer_dir.empty()) {
            const auto asan = "detect_leaks=1:abort_on_error=1:log_path=" + (sanitizer_dir / "asan").string();
            const auto ubsan = "halt_on_error=1:print_stacktrace=1:log_path=" + (sanitizer_dir / "ubsan").string();
            if (setenv("ASAN_OPTIONS", asan.c_str(), 1) != 0 || setenv("UBSAN_OPTIONS", ubsan.c_str(), 1) != 0) {
                _exit(127);
            }
        }
        execv(argv[0], argv.data());
        _exit(127);
    }

    out_writer.reset();
    err_writer.reset();
    std::string out;
    std::string err;
    auto collect = [](Fd& fd, std::string& text) {
        char buffer[8192];
        for (int i = 0; i < 16 && fd.get() >= 0; ++i) {
            const auto size = read(fd.get(), buffer, sizeof(buffer));
            if (size > 0) {
                text.append(buffer, static_cast<std::size_t>(size));
            } else if (size == 0) {
                fd.reset();
            } else if (errno == EAGAIN) {
                return;
            } else if (errno != EINTR) {
                throw std::system_error(errno, std::generic_category(), "Cannot read process output");
            }
        }
    };
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    int status = 0;
    while (true) {
        collect(out_reader, out);
        collect(err_reader, err);
        const pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            collect(out_reader, out);
            collect(err_reader, err);
            break;
        }
        if (waited < 0 && errno != EINTR) {
            throw std::system_error(errno, std::generic_category(), "waitpid failed");
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(-pid, SIGKILL);
            kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
            throw std::runtime_error("Process timeout");
        }
        pollfd descriptors[2] = {{out_reader.get(), POLLIN, 0}, {err_reader.get(), POLLIN, 0}};
        poll(descriptors, 2, 10);
    }

    const int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    return {code, std::move(out), std::move(err)};
}

void expect_invalid(const Result& result) {
    expect_code(result, 1);
    expect_contains(result.err, "Error:");
    expect_equal(result.out, "");
}

struct ConfigTest {
    TempDir temp;
    fs::path config = temp.path / "config.yaml";
    fs::path data = temp.path / "data";

    ConfigTest() {
        fs::create_directory(data);
    }

    Result invoke(const fs::path& file = {}, const fs::path& cwd = "/") {
        return run({binary.string(), "--check-config", (file.empty() ? config : file).string()}, cwd);
    }

    Result check(const std::string& text, const fs::path& cwd = "/") {
        write_file(config, text);
        return invoke(config, cwd);
    }
};

std::string test_invalid_arguments() {
    const auto help = run({binary.string(), "--help"});
    expect_code(help, 0);
    expect_contains(help.out, "--check-config");
    const std::vector<std::vector<std::string>> cases = {
        {}, {"one", "two"}, {"--check-config"}, {"--unknown"}
    };
    for (const auto& args : cases) {
        std::vector<std::string> command = {binary.string()};
        command.insert(command.end(), args.begin(), args.end());
        try {
            const auto result = run(command);
            expect_code(result, 1);
            expect_contains(result.err, "Usage:");
        } catch (const std::exception& error) {
            std::string text;
            for (const auto& arg : args) {
                text += show_text(arg) + ' ';
            }
            throw std::runtime_error("Аргументы: " + (text.empty() ? "нет" : text) +
                                     "\n" + error.what());
        }
    }
    return "справка вернула 0; проверено неверных наборов аргументов: " + std::to_string(cases.size()) +
           "; каждый вернул код 1 и Usage в stderr";
}

std::string test_config_paths() {
    ConfigTest config;
    const auto other = config.temp.path / "папка с пробелами";
    fs::create_directory(other);
    fs::create_directory_symlink(config.data, config.temp.path / "link");
    const auto result = config.check("paths: [data, ./data, link, '" + other.string() + "']\n", other);
    expect_code(result, 0);
    expect_equal(result.out, config.data.string() + "\n" + other.string() + "\n");
    const auto relative = config.invoke("config.yaml", config.temp.path);
    expect_code(relative, 0);
    expect_equal(relative.out, result.out);
    return "абсолютный и относительный пути, пробелы и кириллица работают; дубликаты и ссылка объединены; cwd не влияет на результат";
}

std::string test_config_errors() {
    ConfigTest config;
    expect_invalid(config.invoke());
    write_file(config.temp.path / "file.txt", "data");
    const std::vector<std::string> cases = {
        "", "[]", "value", "{}", "other: []", "paths: null",
        "paths: data", "paths: {}", "paths: []", "paths: [null]",
        "paths: [[]]", "paths: [{folder: data}]", "paths: [\"\"]",
        "paths: [\"data\\0ignored\"]", "paths: [\n",
        "paths: [missing]\n", "paths: [file.txt]\n"
    };
    for (const auto& text : cases) {
        try {
            expect_invalid(config.check(text));
        } catch (const std::exception& error) {
            throw std::runtime_error("Конфиг: " + show_text(text) + "\n" + error.what());
        }
    }
    expect_invalid(config.invoke(config.data));
    fs::remove(config.config);
    expect(mkfifo(config.config.c_str(), 0600) == 0, "Cannot create FIFO");
    expect_invalid(config.invoke());
    fs::remove(config.config);
    write_file(config.config, "paths: [data]\n");
    fs::create_symlink(config.config, config.temp.path / "link.yaml");
    expect_code(config.invoke(config.temp.path / "link.yaml"), 0);
    return "отклонены " + std::to_string(cases.size()) +
           " неверных YAML/путей, отсутствующий конфиг, каталог и FIFO; код 1 без зависания; ссылка на обычный YAML принята";
}

std::string test_permissions() {
    ConfigTest config;
    write_file(config.config, "paths: [data]\n");
    const std::pair<fs::path, fs::perms> cases[] = {
        {config.data, fs::perms::none},
        {config.data, fs::perms::owner_read},
        {config.config, fs::perms::none}
    };
    for (const auto& [path, mode] : cases) {
        Permissions permissions(path, mode);
        try {
            expect_invalid(config.invoke());
        } catch (const std::exception& error) {
            throw std::runtime_error("Путь: " + path.string() + "\n" + error.what());
        }
    }
    expect_code(config.invoke(), 0);
    return "каталоги с правами 000 и 400 и конфиг 000 отклонены с кодом 1; после восстановления прав конфиг принят";
}

std::string test_build() {
    TempDir temp;
    const auto project = fs::path(__FILE__).parent_path().parent_path();
    const auto copy = temp.path / "project with spaces";
    const auto work = temp.path / "build temp";
    fs::create_directory(copy);
    fs::create_directory(work);
    for (const auto& name : {"src", "include", "CMakeLists.txt", "build.sh"}) {
        fs::copy(project / name, copy / name, fs::copy_options::recursive);
    }
    const auto output = copy / "bin" / "disk_monitor";
    const std::vector<std::string> command = {
        "/usr/bin/env", "TMPDIR=" + work.string(), "/bin/bash", (copy / "build.sh").string()
    };
    for (int i = 0; i < 2; ++i) {
        const auto result = run(command, "/", 30s);
        expect_code(result, 0);
        expect_contains(result.out, "Built: " + output.string());
        expect(access(output.c_str(), X_OK) == 0, "Собранный файл не исполняемый");
        expect(fs::is_empty(work), "Временная папка сборки не очищена");
        expect(std::distance(fs::directory_iterator(copy / "bin"), fs::directory_iterator()) == 1,
               "В bin должен оставаться один исполняемый файл");
        expect_code(run({output.string(), "--help"}), 0);
    }
    const auto saved_binary = read_file(output);
    const auto cmake = read_file(copy / "CMakeLists.txt");
    write_file(copy / "CMakeLists.txt", cmake + "\nmessage(FATAL_ERROR \"Build failure test\")\n");
    const auto bad_cmake = run(command, "/", 30s);
    expect(bad_cmake.code != 0, "Ошибка CMake не привела к ошибке сборки");
    expect_contains(bad_cmake.err, "Build failure test");
    expect(fs::is_empty(work), "Временная папка осталась после ошибки CMake");
    expect(read_file(output) == saved_binary, "Ошибка CMake изменила рабочий бинарник");
    write_file(copy / "CMakeLists.txt", cmake);
    const auto main = copy / "src" / "main.cpp";
    write_file(main, read_file(main) + "\nstatic int unused_test;\n");
    const auto warning = run(command, "/", 30s);
    expect(warning.code != 0, "Предупреждение не остановило сборку");
    expect_contains(warning.err, "-Werror");
    expect(fs::is_empty(work), "Временная папка осталась после ошибки компиляции");
    expect(read_file(output) == saved_binary, "Ошибка компиляции изменила рабочий бинарник");
    return "чистая и повторная сборки из / в пути с пробелами прошли; "
           "ошибки CMake и -Werror оставили прежний бинарник, временные папки очищены";
}

std::string read_log(const fs::path& path, std::uintmax_t offset) {
    return fs::exists(path) ? read_file(path, offset) : "";
}

std::string wait_for(const fs::path& path, std::uintmax_t offset,
                     const std::vector<std::string>& messages) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto text = read_log(path, offset);
        if (std::all_of(messages.begin(), messages.end(), [&](const auto& message) {
            return text.find(message) != std::string::npos;
        })) {
            return text;
        }
        std::this_thread::sleep_for(50ms);
    }
    std::string expected;
    for (const auto& message : messages) {
        expected += "\n  " + show_text(message);
    }
    throw std::runtime_error("За 5 секунд в " + path.string() + " не найдены все сообщения:" +
                             expected + "\nПолучено: " + show_text(read_log(path, offset)));
}

struct LogTest {
    TempDir temp;
    fs::path system_log = "/var/log/syslog";
    fs::path file_log = "/var/log/os_lab1.log";
    std::uintmax_t system_offset = fs::file_size(system_log);
    std::uintmax_t file_offset = fs::exists(file_log) ? fs::file_size(file_log) : 0;
    std::string token = temp.path.filename().string();
};

std::vector<std::string> process_fields(pid_t pid) {
    std::ifstream file(fs::path("/proc") / std::to_string(pid) / "stat");
    std::string line;
    expect(static_cast<bool>(std::getline(file, line)), "Cannot read process stat");
    const auto end = line.rfind(')');
    expect(end != std::string::npos, "Invalid process stat");
    std::istringstream fields(line.substr(end + 1));
    return {std::istream_iterator<std::string>(fields), std::istream_iterator<std::string>()};
}

std::string process_record(pid_t pid) {
    const auto fields = process_fields(pid);
    expect(fields.size() > 19, "Missing process start time");
    struct stat info{};
    const auto path = fs::path("/proc") / std::to_string(pid) / "exe";
    expect(stat(path.c_str(), &info) == 0, "Cannot inspect process executable");
    return std::to_string(pid) + ' ' + fields[19] + ' ' + std::to_string(info.st_dev) + ' ' +
           std::to_string(info.st_ino) + '\n';
}

std::vector<std::string> monitor_args(const fs::path& config, const fs::path& state) {
    return {"/usr/bin/env", "OS_LAB_STATE_DIR=" + state.string(), binary.string(), config.string()};
}

class Monitor {
public:
    explicit Monitor(const fs::path& config, const fs::path& shared_state = {},
                     const fs::path& cwd = "/", bool close_input = false)
        : state(shared_state.empty() ? temp.path / "state" : shared_state) {
        result = run(monitor_args(config, state), cwd, 20s, close_input);
        expect_code(result, 0);
        std::ifstream file(pid_path());
        expect(static_cast<bool>(file >> pid) && pid > 1, "Cannot read daemon PID");
        try {
            expect_equal(result.out, "Monitoring started\n");
            check_running();
        } catch (...) {
            stop();
            throw;
        }
    }

    ~Monitor() {
        stop();
    }

    Monitor(const Monitor&) = delete;
    Monitor& operator=(const Monitor&) = delete;

    void check_running() {
        int status = 0;
        const pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            pid = -1;
            throw std::runtime_error("Монитор завершился: " + result.err);
        }
        expect(waited == 0, "Cannot check monitor");
        expect_equal(result.err, "");
    }

    fs::path pid_path() const { return state / "disk_monitor.pid"; }
    fs::path state_path() const { return state; }
    pid_t process_id() const { return pid; }

    void signal(int value) {
        expect(pid > 1 && kill(pid, value) == 0, "Cannot signal test daemon");
    }

    void suspend() {
        signal(SIGSTOP);
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (std::chrono::steady_clock::now() < deadline) {
            if (process_fields(pid)[0] == "T") {
                return;
            }
            std::this_thread::sleep_for(10ms);
        }
        throw std::runtime_error("Cannot suspend test daemon");
    }

    int wait_for_exit() {
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        int status = 0;
        while (std::chrono::steady_clock::now() < deadline) {
            const auto result = waitpid(pid, &status, WNOHANG);
            if (result == pid) {
                pid = -1;
                expect(WIFEXITED(status), "Daemon terminated by signal: " + std::to_string(WTERMSIG(status)));
                return WEXITSTATUS(status);
            }
            expect(result == 0 || (result < 0 && errno == EINTR), "Cannot wait for test daemon");
            std::this_thread::sleep_for(10ms);
        }
        throw std::runtime_error("Daemon did not stop in 2 seconds");
    }

    void stop() {
        if (pid <= 0) {
            return;
        }
        int status = 0;
        const pid_t initial = waitpid(pid, &status, WNOHANG);
        if (initial == pid || (initial < 0 && errno == ECHILD)) {
            pid = -1;
            return;
        }
        kill(pid, SIGCONT);
        kill(pid, SIGTERM);
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (true) {
            const auto result = waitpid(pid, &status, WNOHANG);
            if (result == pid || (result < 0 && errno == ECHILD)) {
                break;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                kill(pid, SIGKILL);
                while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
                }
                break;
            }
            std::this_thread::sleep_for(10ms);
        }
        pid = -1;
    }

private:
    TempDir temp;
    fs::path state;
    Result result;
    pid_t pid = -1;
};

std::string event_line(const fs::path& path, const std::string& type) {
    return "path=" + show_text(path.string()) + " events=" + type;
}

std::string test_file_operations() {
    ConfigTest config;
    LogTest logs;
    write_file(config.config, "paths: [data]\n");
    Monitor monitor(config.config);
    const auto file = config.data / "file.txt";
    write_file(file, "hello");
    expect_equal(read_file(file), "hello");
    fs::permissions(file, fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read);
    fs::remove(file);
    const auto folder = config.data / "folder";
    fs::create_directory(folder);
    fs::remove(folder);
    const auto text = wait_for(logs.file_log, logs.file_offset, {
        event_line(file, "IN_CREATE"), event_line(file, "IN_OPEN"),
        event_line(file, "IN_MODIFY"), event_line(file, "IN_ACCESS"),
        event_line(file, "IN_CLOSE_WRITE"), event_line(file, "IN_CLOSE_NOWRITE"),
        event_line(file, "IN_ATTRIB"), event_line(file, "IN_DELETE"),
        event_line(folder, "IN_CREATE|IN_ISDIR"), event_line(folder, "IN_DELETE|IN_ISDIR")
    });
    expect_contains(text, "directory=1 cookie=0");
    expect_contains(text, "directory=0 cookie=0");
    expect_absent(read_log(logs.system_log, logs.system_offset), event_line(file, "IN_CREATE"));
    monitor.check_running();
    return "в отдельном журнале найдены create, open, modify, access, оба close, attrib, delete; "
           "создание и удаление папки отмечены directory=1, файл — directory=0";
}

std::uint32_t find_cookie(const std::string& text, const fs::path& path, const char* type) {
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.find(event_line(path, type)) != std::string::npos) {
            const auto offset = line.find("cookie=");
            expect(offset != std::string::npos, "В событии нет cookie: " + line);
            return static_cast<std::uint32_t>(std::stoul(line.substr(offset + 7)));
        }
    }
    throw std::runtime_error("Не найдено событие: " + event_line(path, type));
}

std::string test_move_cookie() {
    ConfigTest config;
    LogTest logs;
    const auto second = config.temp.path / "second";
    fs::create_directory(second);
    write_file(config.config, "paths: [data, second]\n");
    Monitor monitor(config.config);
    auto before = config.data / "before.txt";
    write_file(before, "data");
    wait_for(logs.file_log, logs.file_offset, {event_line(before, "IN_CLOSE_WRITE")});
    for (const auto& after : {config.data / "renamed.txt", second / "after.txt"}) {
        const auto offset = fs::file_size(logs.file_log);
        fs::rename(before, after);
        const auto text = wait_for(logs.file_log, offset,
                                  {event_line(before, "IN_MOVED_FROM"), event_line(after, "IN_MOVED_TO")});
        const auto from = find_cookie(text, before, "IN_MOVED_FROM");
        const auto to = find_cookie(text, after, "IN_MOVED_TO");
        expect(from != 0 && from == to, "Cookie: from=" + std::to_string(from) + ", to=" + std::to_string(to));
        expect_equal(read_file(after), "data");
        before = after;
    }
    monitor.check_running();
    return "переименование в одной папке и перенос между папками дали from/to с одинаковым ненулевым cookie; содержимое сохранено";
}

std::string test_special_filenames() {
    ConfigTest config;
    LogTest logs;
    write_file(config.config, "paths: [data]\n");
    Monitor monitor(config.config);
    const auto file = config.data / "имя с пробелом\n\r\t\"\\%s.txt";
    write_file(file, "data");
    const auto expected = event_line(file, "IN_CREATE");
    const auto text = wait_for(logs.file_log, logs.file_offset, {expected});
    std::istringstream lines(text);
    std::string line;
    bool found = false;
    while (std::getline(lines, line)) {
        if (line.find(expected) != std::string::npos) {
            expect_contains(line, "directory=0 cookie=0");
            found = true;
        }
    }
    expect(found, "Событие должно целиком помещаться в одной строке");
    monitor.check_running();
    return "кириллица, пробел, %s сохранены; перевод строки, CR, табуляция, кавычка и слеш "
           "экранированы, запись занимает одну строку";
}

std::string test_batch_events() {
    ConfigTest config;
    LogTest logs;
    write_file(config.config, "paths: [data]\n");
    Monitor monitor(config.config);
    const long limit = pathconf(config.data.c_str(), _PC_NAME_MAX);
    expect(limit >= 32 && limit <= 4096, "Cannot determine filename limit");
    monitor.suspend();
    std::vector<std::string> messages;
    for (int i = 0; i < 64; ++i) {
        const auto prefix = std::to_string(i) + '-';
        const auto path = config.data / (prefix + std::string(static_cast<std::size_t>(limit) - prefix.size(), 'a'));
        write_file(path, "data");
        messages.push_back(event_line(path, "IN_CREATE"));
        messages.push_back(event_line(path, "IN_CLOSE_WRITE"));
    }
    monitor.signal(SIGCONT);
    wait_for(logs.file_log, logs.file_offset, messages);
    monitor.check_running();
    return "для всех 64 файлов с именем " + std::to_string(limit) +
           " байт найдены create и close_write; очередь длиннее буфера разобрана без обрезки имён";
}

std::string test_deleted_directory() {
    ConfigTest config;
    LogTest logs;
    const auto alive = config.temp.path / "alive";
    fs::create_directory(alive);
    write_file(config.config, "paths: [data, alive]\n");
    Monitor monitor(config.config);
    fs::remove(config.data);
    wait_for(logs.file_log, logs.file_offset,
             {event_line(config.data, "IN_DELETE_SELF"), event_line(config.data, "IN_IGNORED")});
    wait_for(logs.system_log, logs.system_offset, {"Watch lost: " + show_text(config.data.string())});
    const auto file = alive / "after-delete.txt";
    write_file(file, "data");
    wait_for(logs.file_log, logs.file_offset, {event_line(file, "IN_CLOSE_WRITE")});
    monitor.check_running();
    return "IN_DELETE_SELF и IN_IGNORED записаны без имени; потеря наблюдения отмечена, "
           "второй каталог продолжает работать";
}

std::string test_moved_directory() {
    ConfigTest config;
    LogTest logs;
    const auto alive = config.temp.path / "alive";
    const auto moved = config.temp.path / "moved";
    fs::create_directory(alive);
    write_file(config.config, "paths: [data, alive]\n");
    Monitor monitor(config.config);
    fs::rename(config.data, moved);
    wait_for(logs.file_log, logs.file_offset,
             {event_line(config.data, "IN_MOVE_SELF"), event_line(config.data, "IN_IGNORED")});
    wait_for(logs.system_log, logs.system_offset, {"Watch lost: " + show_text(config.data.string())});
    write_file(moved / "hidden-after-move.txt", "data");
    const auto file = alive / "after-move.txt";
    write_file(file, "data");
    const auto text = wait_for(logs.file_log, logs.file_offset, {event_line(file, "IN_CLOSE_WRITE")});
    expect_absent(text, "hidden-after-move.txt");
    monitor.check_running();
    return "IN_MOVE_SELF и IN_IGNORED найдены; старый путь больше не используется, "
           "наблюдение за вторым каталогом сохранено";
}

std::string test_moved_parent_directory() {
    const std::vector<std::string> configs = {
        "paths: [data, data/nested, alive]\n",
        "paths: [data/nested, alive]\n",
        "paths: [., data/nested, alive]\n"
    };
    for (std::size_t i = 0; i < configs.size(); ++i) {
        ConfigTest config;
        LogTest logs;
        const auto nested = config.data / "nested";
        const auto alive = config.temp.path / "alive";
        const auto moved = config.temp.path / "moved";
        fs::create_directory(nested);
        fs::create_directory(alive);
        write_file(config.config, configs[i]);
        Monitor monitor(config.config);
        fs::rename(config.data, moved);
        if (i == 0) {
            wait_for(logs.file_log, logs.file_offset, {event_line(config.data, "IN_MOVE_SELF")});
        }
        fs::create_directories(nested);
        write_file(moved / "nested" / "wrong-path.txt", "data");
        write_file(nested / "replacement.txt", "data");
        const auto marker = alive / "marker.txt";
        write_file(marker, "data");
        const auto text = wait_for(logs.file_log, logs.file_offset, {event_line(marker, "IN_CLOSE_WRITE")});
        expect_absent(text, "wrong-path.txt");
        expect_absent(text, "replacement.txt");
        wait_for(logs.system_log, logs.system_offset, {"Watch lost: " + show_text(nested.string())});
        monitor.check_running();
    }
    return "проверены три случая: наблюдается родитель, только подпапка или более дальний предок; "
           "новая папка на старом месте не путается с прежней, независимый каталог работает";
}

std::string test_events_before_directory_removal() {
    LogTest logs;
    Logger logger;
    const auto data = logs.temp.path / "data";
    const auto nested = data / "nested";
    fs::create_directories(nested);
    FileWatcher watcher({data, nested});
    const auto first = data / "first.txt";
    const auto second = nested / "second.txt";
    write_file(first, "data");
    write_file(second, "data");
    fs::remove(first);
    fs::remove(second);
    fs::remove(nested);
    fs::remove(data);
    for (int i = 0; i < 10; ++i) {
        watcher.wait(20);
    }
    wait_for(logs.file_log, logs.file_offset, {
        event_line(first, "IN_CREATE"), event_line(first, "IN_DELETE"),
        event_line(second, "IN_CREATE"), event_line(second, "IN_DELETE"),
        event_line(nested, "IN_DELETE_SELF"), event_line(data, "IN_DELETE_SELF")
    });
    return "создание и удаление обоих файлов из очереди записаны даже после удаления "
           "наблюдаемых папок; DELETE_SELF для обеих папок сохранены";
}

void append_event(std::string& buffer, int wd, std::uint32_t mask,
                  std::uint32_t cookie, const std::string& name = {}) {
    const auto len = name.empty() ? 0U : static_cast<unsigned int>((name.size() + 16) / 16 * 16);
    const inotify_event event{wd, mask, cookie, len};
    buffer.append(reinterpret_cast<const char*>(&event), sizeof(event));
    if (len != 0) {
        buffer += name;
        buffer.append(len - name.size(), '\0');
    }
}

std::string test_event_buffers() {
    {
        std::string buffer;
        for (int i = 0; i < 2000; ++i) {
            append_event(buffer, i + 1, IN_CREATE | IN_ISDIR, i, "folder-" + std::to_string(i));
        }
        append_event(buffer, 1, IN_DELETE_SELF, 0);
        append_event(buffer, -1, IN_Q_OVERFLOW, 0);
        const std::string unaligned = "x" + buffer;
        const auto events = parse_events(unaligned.data() + 1, buffer.size());
        expect(events.size() == 2002, "Ожидалось 2002 события, получено " + std::to_string(events.size()));
        for (int i = 0; i < 2000; ++i) {
            const auto& event = events[i];
            expect(event.wd == i + 1 && event.mask == (IN_CREATE | IN_ISDIR) &&
                   event.cookie == static_cast<std::uint32_t>(i) && event.name == "folder-" + std::to_string(i),
                   "Неверное событие с номером " + std::to_string(i));
        }
        expect(events[2000].name.empty() && events[2000].mask == IN_DELETE_SELF,
               "Событие без имени разобрано неверно");
        expect(events[2001].wd == -1 && events[2001].mask == IN_Q_OVERFLOW,
               "Событие переполнения разобрано неверно");
        expect(parse_events(buffer.data(), 0).empty(), "Пустой буфер должен дать пустой список");
    }
    {
        std::string valid;
        append_event(valid, 1, IN_CREATE, 0, "file");
        auto no_null = valid;
        std::fill(no_null.begin() + sizeof(inotify_event), no_null.end(), 'x');
        std::string huge;
        const inotify_event event{1, IN_CREATE, 0, UINT32_MAX};
        huge.append(reinterpret_cast<const char*>(&event), sizeof(event));
        const std::vector<std::string> cases = {
            "x", valid.substr(0, sizeof(inotify_event) - 1), valid.substr(0, valid.size() - 1),
            no_null, huge, valid + "x"
        };
        for (std::size_t i = 0; i < cases.size(); ++i) {
            bool rejected = false;
            try {
                parse_events(cases[i].data(), cases[i].size());
            } catch (const std::runtime_error& error) {
                expect_contains(error.what(), "Invalid inotify");
                rejected = true;
            }
            expect(rejected, "Повреждённый буфер принят: случай " + std::to_string(i + 1));
        }
    }
    {
        const FileEvent event{1, IN_CREATE | IN_MODIFY | IN_ISDIR, 123, "child"};
        const auto text = format_event("/tmp/name\n\t\"\\\1", event);
        expect_equal(text, "path=\"/tmp/name\\n\\t\\\"\\\\\\x01\" "
                           "events=IN_MODIFY|IN_CREATE|IN_ISDIR mask=0x40000102 directory=1 cookie=123");
        expect_contains(format_event("/gone", {1, IN_DELETE | IN_ISDIR, 0, "gone"}), "directory=1");
        expect_contains(format_event("/gone", {1, IN_DELETE, 0, "gone"}), "directory=0");
        expect_contains(format_event("/self", {1, IN_OPEN, 0, ""}), "directory=1");
        expect_contains(format_event("/unknown", {1, 0x00800000, 0, "file"}), "mask=0x800000");
    }
    return "2002 записи из невыровненного буфера разобраны; 6 повреждённых буферов отклонены; составная маска, cookie и тип объекта сохранены";
}

std::size_t count_fds() {
    return static_cast<std::size_t>(std::distance(fs::directory_iterator("/proc/self/fd"),
                                                 fs::directory_iterator()));
}


std::string test_overflow_recovery() {
    LogTest logs;
    Logger logger;
    const auto first = logs.temp.path / "first";
    const auto second = logs.temp.path / "second";
    fs::create_directory(first);
    fs::create_directory(second);
    const auto missing = logs.temp.path / "missing";
    fs::create_directory(missing);
    const auto before = count_fds();
    {
        FileWatcher watcher({first});
        {
            FileWatcher next({second, missing});
            watcher.swap(next);
        }
        expect(count_fds() == before + 1, "Previous inotify descriptor was not closed");
        fs::remove(missing);
        watcher.process_events({{-1, IN_Q_OVERFLOW, 0, ""}, {1, IN_CREATE, 0, "stale-event.txt"}});
        write_file(first / "old.txt", "data");
        write_file(second / "new.txt", "data");
        watcher.drain();
        const auto text = wait_for(logs.file_log, logs.file_offset,
                                  {event_line(second / "new.txt", "IN_CLOSE_WRITE")});
        expect_absent(text, (first / "old.txt").string());
        expect_absent(text, "stale-event.txt");
        wait_for(logs.system_log, logs.system_offset,
                 {"Inotify overflow: events lost", "Watches restored: 1",
                  "Cannot watch: " + show_text(missing.string())});
        expect(count_fds() == before + 1, "Overflow leaked a descriptor after watch change");
    }
    expect(count_fds() == before, "Changed watcher left a descriptor");
    return "после смены набора синтетический overflow восстанавливает новую папку; прежняя не наблюдается, исчезнувшая отмечена ошибкой, хвост отброшен, лишних fd нет";
}


int first_watch() {
    for (const auto& entry : fs::directory_iterator("/proc/self/fd")) {
        std::error_code error;
        if (fs::read_symlink(entry.path(), error) != "anon_inode:inotify") {
            continue;
        }
        std::ifstream info(fs::path("/proc/self/fdinfo") / entry.path().filename());
        std::string line;
        while (std::getline(info, line)) {
            if (line.find("inotify wd:") == 0) {
                return std::stoi(line.substr(11), nullptr, 16);
            }
        }
    }
    throw std::runtime_error("Не найден wd в /proc/self/fdinfo");
}

std::string test_unmount_and_ignored() {
    LogTest logs;
    Logger logger;
    for (bool unmount : {true, false}) {
        const auto path = logs.temp.path / (unmount ? "unmount" : "ignored");
        fs::create_directory(path);
        FileWatcher watcher({path});
        const int wd = first_watch();
        std::vector<FileEvent> events;
        if (unmount) {
            events.push_back({wd, IN_UNMOUNT, 0, ""});
        }
        events.push_back({wd, IN_IGNORED, 0, ""});
        watcher.process_events(events);
        const auto text = wait_for(logs.file_log, logs.file_offset,
                                  {event_line(path, "IN_IGNORED")});
        if (unmount) {
            expect_contains(text, event_line(path, "IN_UNMOUNT"));
        }
        const auto message = "Watch lost: " + show_text(path.string());
        const auto service = wait_for(logs.system_log, logs.system_offset, {message});
        expect(service.find(message, service.find(message) + message.size()) == std::string::npos,
               "Потеря одного наблюдения записана дважды");
        expect_absent(service, "Unknown watch:");
    }
    return "синтетические IN_UNMOUNT + IN_IGNORED и отдельный IN_IGNORED обработаны; "
           "события записаны, потеря каждого наблюдения отмечена ровно один раз";
}

std::string test_watch_resources() {
    TempDir temp;
    Logger logger;
    const auto before = count_fds();
    {
        FileWatcher watcher({temp.path});
        expect(count_fds() == before + 1, "Наблюдение должно открыть один дескриптор");
        bool flags_checked = false;
        for (const auto& entry : fs::directory_iterator("/proc/self/fd")) {
            std::error_code error;
            if (fs::read_symlink(entry.path(), error) == "anon_inode:inotify") {
                const int fd = std::stoi(entry.path().filename());
                expect((fcntl(fd, F_GETFL) & O_NONBLOCK) != 0, "Не установлен O_NONBLOCK");
                expect((fcntl(fd, F_GETFD) & FD_CLOEXEC) != 0, "Не установлен FD_CLOEXEC");
                flags_checked = true;
            }
        }
        expect(flags_checked, "Не найден дескриптор inotify");
        for (int i = 0; i < 3; ++i) {
            watcher.process_events({{-1, IN_Q_OVERFLOW, 0, ""}});
            expect(count_fds() == before + 1, "Утечка дескриптора после переполнения");
        }
    }
    expect(count_fds() == before, "Дескриптор не закрыт после разрушения FileWatcher");
    bool rejected = false;
    try {
        FileWatcher watcher({temp.path, temp.path / "missing"});
    } catch (const std::system_error& error) {
        expect_contains(error.what(), "Cannot watch:");
        rejected = true;
    }
    expect(rejected, "Запуск с исчезнувшим каталогом должен завершиться ошибкой");
    expect(count_fds() == before, "Утечка дескриптора при ошибке запуска");
    return "O_NONBLOCK и FD_CLOEXEC установлены; три восстановления, разрушение объекта "
           "и ошибка запуска не оставили лишних дескрипторов";
}

void send_log(const char* name, int facility, const std::string& message) {
    openlog(name, LOG_NDELAY, facility);
    syslog(LOG_INFO, "%s", message.c_str());
    closelog();
}

std::string test_logging() {
    LogTest logs;
    {
        Logger logger;
        Logger::info("service-" + logs.token);
        Logger::error("error-" + logs.token);
        Logger::event("event-" + logs.token + " literal %s %n %m %%");
    }
    const auto events = wait_for(logs.file_log, logs.file_offset, {"event-" + logs.token});
    const auto service = wait_for(logs.system_log, logs.system_offset,
                                 {"service-" + logs.token, "error-" + logs.token});
    expect_contains(events, "literal %s %n %m %%");
    expect_absent(events, "service-" + logs.token);
    expect_absent(events, "error-" + logs.token);
    expect_absent(service, "event-" + logs.token);
    send_log("other_monitor", LOG_LOCAL0, "other-" + logs.token);
    send_log("disk_monitor", LOG_LOCAL1, "wrong-facility-" + logs.token);
    send_log("disk_monitor", LOG_LOCAL0, "correct-" + logs.token);
    const auto filtered = wait_for(logs.file_log, logs.file_offset, {"correct-" + logs.token});
    wait_for(logs.system_log, logs.system_offset,
             {"other-" + logs.token, "wrong-facility-" + logs.token});
    expect_absent(filtered, "other-" + logs.token);
    expect_absent(filtered, "wrong-facility-" + logs.token);
    const auto path = logs.temp.path / ("missing-" + logs.token + ".yaml");
    expect_invalid(run({binary.string(), "--check-config", path.string()}));
    wait_for(logs.system_log, logs.system_offset, {path.string()});
    expect_absent(read_log(logs.file_log, logs.file_offset), path.string());
    return "LOG_LOCAL0 disk_monitor попал в отдельный журнал; info, error и ошибка конфига — в syslog; фильтр верен, %s %n %m %% сохранены";
}

class Child {
public:
    explicit Child(pid_t pid) : pid(pid) { expect(pid > 1, "Cannot create child"); }
    ~Child() {
        if (pid > 1) {
            kill(pid, SIGCONT);
            kill(pid, SIGKILL);
            int status;
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
        }
    }
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;

    pid_t id() const { return pid; }

    bool running() {
        int status;
        const auto result = waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            pid = -1;
            return false;
        }
        expect(result == 0, "Cannot inspect child");
        return true;
    }

    int wait() {
        const auto deadline = std::chrono::steady_clock::now() + 20s;
        int status = 0;
        while (std::chrono::steady_clock::now() < deadline) {
            const auto result = waitpid(pid, &status, WNOHANG);
            if (result == pid) {
                pid = -1;
                return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            }
            expect(result == 0 || (result < 0 && errno == EINTR), "Cannot wait for child");
            std::this_thread::sleep_for(10ms);
        }
        throw std::runtime_error("Child timeout");
    }

private:
    pid_t pid;
};

std::string test_daemon_environment() {
    ConfigTest config;
    LogTest logs;
    write_file(config.config, "paths: [data]\n");
    Fd inherited(open((config.temp.path / "inherited").c_str(), O_RDWR | O_CREAT, 0600));
    expect(inherited.get() >= 0, "Cannot create inherited descriptor");
    Fd high(fcntl(inherited.get(), F_DUPFD, 255));
    expect(high.get() >= 255, "Cannot duplicate inherited descriptor");
    Monitor monitor("config.yaml", {}, config.temp.path);
    wait_for(logs.system_log, logs.system_offset,
             {"Config loaded: " + config.config.string(), "Monitoring started"});
    const auto proc = fs::path("/proc") / std::to_string(monitor.process_id());
    expect_equal(fs::read_symlink(proc / "cwd").string(), "/");
    const auto fields = process_fields(monitor.process_id());
    expect(fields.size() > 19, "Incomplete process stat");
    expect(fields[2] == fields[3] && fields[3] != std::to_string(monitor.process_id()),
           "Daemon is a session leader");
    expect_equal(fields[4], "0");
    for (int fd = 0; fd < 3; ++fd) {
        expect_equal(fs::read_symlink(proc / "fd" / std::to_string(fd)).string(), "/dev/null");
    }
    expect(!fs::exists(proc / "fd" / std::to_string(high.get())), "Inherited descriptor is open");
    expect_equal(read_file(monitor.pid_path()), process_record(monitor.process_id()));
    struct stat info{};
    for (const auto& path : {monitor.state_path(), monitor.pid_path(),
                             monitor.state_path() / "disk_monitor.lock"}) {
        expect(stat(path.c_str(), &info) == 0, "Cannot inspect state permissions");
        expect(info.st_uid == geteuid(), "Wrong state owner");
        expect((info.st_mode & 0777) == (path == monitor.state_path() ? 0700 : 0600),
               "State permissions are too broad");
    }
    expect_contains(read_file(proc / "status"), "Umask:\t0077");
    write_file(config.data / "background.txt", "data");
    wait_for(logs.file_log, logs.file_offset, {event_line(config.data / "background.txt", "IN_CREATE")});
    return "запуск в фоне записан в syslog; cwd=/, tty=0, демон не лидер сеанса, fd 0–2=/dev/null, fd 255 закрыт; "
           "PID и время старта совпали с /proc, права 700/600, umask 0077; относительный конфиг работает";
}

std::string test_closed_standard_input() {
    ConfigTest config;
    write_file(config.config, "paths: [data]\n");
    Monitor monitor(config.config, {}, "/", true);
    const auto input = fs::path("/proc") / std::to_string(monitor.process_id()) / "fd/0";
    expect_equal(fs::read_symlink(input).string(), "/dev/null");
    monitor.check_running();
    return "запуск с закрытым stdin успешен; служебные дескрипторы сохранены, fd 0=/dev/null";
}

std::string test_daemon_stop() {
    ConfigTest config;
    LogTest logs;
    write_file(config.config, "paths: [data]\n");
    Monitor monitor(config.config);
    const auto pid = monitor.process_id();
    monitor.signal(SIGTERM);
    expect(monitor.wait_for_exit() == 0, "Daemon did not stop normally");
    expect(!fs::exists(monitor.pid_path()), "PID file was not removed");
    expect(fs::is_regular_file(monitor.state_path() / "disk_monitor.lock"), "Lock file was removed");
    wait_for(logs.system_log, logs.system_offset,
             {"disk_monitor[" + std::to_string(pid) + "]: Monitoring stopped"});
    return "SIGTERM завершил демон с кодом 0; собственный PID-файл удалён, lock-файл сохранён; завершение есть в syslog";
}

std::string test_daemon_restart() {
    ConfigTest config;
    LogTest logs;
    write_file(config.config, "paths: [data]\n");
    Monitor first(config.config);
    const auto old_pid = first.process_id();
    const auto saved = read_file(first.pid_path());
    write_file(config.config, "paths: [missing]\n");
    expect_invalid(run(monitor_args(config.config, first.state_path())));
    expect_equal(read_file(first.pid_path()), saved);
    first.check_running();
    write_file(config.config, "paths: [data]\n");
    Monitor second(config.config, first.state_path());
    expect(second.process_id() != old_pid, "PID did not change");
    int status = 0;
    expect(waitpid(old_pid, &status, WNOHANG) == old_pid, "Old daemon is still running");
    expect(WIFEXITED(status) && WEXITSTATUS(status) == 0, "Old daemon was not stopped normally");
    expect_equal(read_file(second.pid_path()), process_record(second.process_id()));
    wait_for(logs.system_log, logs.system_offset,
             {"disk_monitor[" + std::to_string(old_pid) + "]: Monitoring stopped"});
    second.check_running();
    return "ошибочный конфиг сохранил прежний демон; старый PID " + std::to_string(old_pid) + " завершился с кодом 0; новый PID " +
           std::to_string(second.process_id()) + " работает и записан в PID-файл";
}

std::string test_invalid_pid_records() {
    ConfigTest config;
    write_file(config.config, "paths: [data]\n");
    const auto state = config.temp.path / "state";
    fs::create_directory(state);
    const std::vector<std::string> records = {
        "", "text\n", "-1 1 1 1\n", "0 1 1 1\n", "1 1 1 1\n",
        "99999999999999999999 1 1 1\n", "22 1 1 1 extra\n", "22 -1 1 1\n",
        std::string(300, '9'), "2147483647 1 1 1\n"
    };
    for (const auto& record : records) {
        write_file(state / "disk_monitor.pid", record);
        Monitor monitor(config.config, state);
        expect_equal(read_file(monitor.pid_path()), process_record(monitor.process_id()));
        monitor.stop();
        expect(!fs::exists(monitor.pid_path()), "PID file was not removed");
    }
    return "проверено записей: " + std::to_string(records.size()) +
           "; пустые, повреждённые и устаревшие данные заменены новым корректным PID";
}

std::string test_foreign_pid() {
    ConfigTest config;
    write_file(config.config, "paths: [data]\n");
    const pid_t pid = fork();
    if (pid == 0) {
        for (;;) {
            pause();
        }
    }
    Child foreign(pid);
    const auto state = config.temp.path / "state";
    fs::create_directory(state);
    write_file(state / "disk_monitor.pid", process_record(foreign.id()));
    Monitor monitor(config.config, state);
    expect(foreign.running(), "Foreign process received a signal");
    monitor.check_running();
    return "запись с настоящими PID, временем старта и inode посторонней программы не привела к её остановке";
}

std::string test_pid_identity() {
    ConfigTest config;
    write_file(config.config, "paths: [data]\n");
    for (int index : {1, 2, 3}) {
        Monitor first(config.config);
        std::istringstream input(read_file(first.pid_path()));
        unsigned long long values[4];
        expect(static_cast<bool>(input >> values[0] >> values[1] >> values[2] >> values[3]), "Invalid PID record");
        ++values[index];
        std::string changed;
        for (auto value : values) {
            changed += std::to_string(value) + ' ';
        }
        write_file(first.pid_path(), changed + '\n');
        Monitor second(config.config, first.state_path());
        first.check_running();
        first.stop();
        expect_equal(read_file(second.pid_path()), process_record(second.process_id()));
        second.check_running();
    }
    return "несовпадение времени старта, device и inode проверено отдельно; старому процессу сигнал не отправлен, новый PID не удалён";
}

std::string test_replaced_executable() {
    ConfigTest config;
    write_file(config.config, "paths: [data]\n");
    const auto original = binary;
    const auto copy = config.temp.path / "disk_monitor";
    const auto replacement = config.temp.path / "new_monitor";
    fs::copy_file(binary, copy);
    binary = copy;
    try {
        Monitor first(config.config);
        const auto old_pid = first.process_id();
        fs::copy_file(copy, replacement);
        fs::rename(replacement, copy);
        const auto executable = fs::read_symlink(fs::path("/proc") / std::to_string(old_pid) / "exe");
        expect_contains(executable.string(), " (deleted)");
        Monitor second(config.config, first.state_path());
        int status;
        expect(waitpid(old_pid, &status, WNOHANG) == old_pid, "Old binary is still running");
        expect(WIFEXITED(status) && WEXITSTATUS(status) == 0, "Old binary was not stopped normally");
        second.check_running();
        binary = original;
    } catch (...) {
        binary = original;
        throw;
    }
    return "после замены бинарника inode изменился и /proc/exe помечен deleted; прежний демон корректно заменён новым";
}

std::string test_changed_pid_cleanup() {
    ConfigTest config;
    write_file(config.config, "paths: [data]\n");
    Monitor monitor(config.config);
    write_file(monitor.pid_path(), "changed record\n");
    monitor.stop();
    expect_equal(read_file(monitor.pid_path()), "changed record\n");
    return "после замены содержимого PID-файла завершение прежнего демона сохранило чужую запись";
}

std::string test_state_file_types() {
    ConfigTest config;
    write_file(config.config, "paths: [data]\n");
    const auto state = config.temp.path / "state";
    fs::create_directory(state);
    for (const auto& name : {"disk_monitor.pid", "disk_monitor.lock"}) {
        const auto path = state / name;
        fs::remove(path);
        expect(mkfifo(path.c_str(), 0600) == 0, "Cannot create FIFO");
        expect_invalid(run(monitor_args(config.config, state)));
        fs::remove(path);
        fs::create_symlink(config.config, path);
        expect_invalid(run(monitor_args(config.config, state)));
        fs::remove(path);
    }
    Monitor monitor(config.config, state);
    return "FIFO и ссылка вместо PID-файла и lock-файла отклонены без зависания; обычный запуск затем успешен";
}

std::string test_state_permissions() {
    ConfigTest config;
    write_file(config.config, "paths: [data]\n");
    const auto state = config.temp.path / "state";
    fs::create_directory(state);
    for (const auto& name : {"disk_monitor.pid", "disk_monitor.lock"}) {
        const auto path = state / name;
        write_file(path, "");
        {
            Permissions permissions(path, fs::perms::none);
            const auto result = run(monitor_args(config.config, state));
            expect_invalid(result);
            expect_contains(result.err, "Permission denied");
        }
        fs::remove(path);
    }
    const auto parent = config.temp.path / "no-access";
    fs::create_directory(parent);
    {
        Permissions permissions(parent, fs::perms::none);
        expect_invalid(run(monitor_args(config.config, parent / "state")));
    }
    expect_invalid(run(monitor_args(config.config, "relative-state")));
    Monitor monitor(config.config, state);
    return "нет прав на PID, lock и родительский каталог — код 1; относительный путь состояния отклонён; блокировки не остались";
}

std::string test_stop_timeout() {
    ConfigTest config;
    write_file(config.config, "paths: [data]\n");
    Monitor monitor(config.config);
    const auto saved = read_file(monitor.pid_path());
    expect(kill(monitor.process_id(), SIGSTOP) == 0, "Cannot suspend test daemon");
    const auto start = std::chrono::steady_clock::now();
    const auto result = run(monitor_args(config.config, monitor.state_path()), "/", 20s);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    expect_invalid(result);
    expect_contains(result.err, "Cannot stop monitor: timeout");
    expect(elapsed >= 4s && elapsed < 8s, "Wrong stop timeout");
    expect_equal(read_file(monitor.pid_path()), saved);
    monitor.check_running();
    monitor.stop();
    return "через 5 секунд новый запуск вернул код 1; остановленный SIGSTOP экземпляр не получил SIGKILL, его PID сохранился";
}

std::string test_lock_timeout() {
    ConfigTest config;
    write_file(config.config, "paths: [data]\n");
    const auto state = config.temp.path / "state";
    fs::create_directory(state);
    Fd lock(open((state / "disk_monitor.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600));
    expect(lock.get() >= 0 && flock(lock.get(), LOCK_EX | LOCK_NB) == 0, "Cannot hold test lock");
    const auto start = std::chrono::steady_clock::now();
    const auto result = run(monitor_args(config.config, state), "/", 20s);
    expect_invalid(result);
    expect_contains(result.err, "Monitor busy");
    const auto elapsed = std::chrono::steady_clock::now() - start;
    expect(elapsed >= 4s && elapsed < 8s, "Wrong lock timeout");
    expect(!fs::exists(state / "disk_monitor.pid"), "Busy launch created a PID file");
    lock.reset();
    Monitor monitor(config.config, state);
    return "занятая блокировка дала Monitor busy через 5 секунд; после её освобождения демон запустился";
}

std::string test_parallel_start() {
    ConfigTest config;
    write_file(config.config, "paths: [data]\n");
    const auto state = config.temp.path / "state";
    auto launch = [&]() {
        const auto pid = fork();
        if (pid == 0) {
            try {
                const auto result = run(monitor_args(config.config, state), "/", 20s);
                _exit(result.code);
            } catch (const std::exception& error) {
                std::cerr << error.what() << '\n';
                _exit(127);
            }
        }
        return pid;
    };
    Child first(launch());
    Child second(launch());
    const auto first_code = first.wait();
    const auto second_code = second.wait();
    pid_t pid = -1;
    std::ifstream file(state / "disk_monitor.pid");
    expect(static_cast<bool>(file >> pid) && pid > 1, "Missing parallel daemon PID");
    Child daemon(pid);
    expect(first_code == 0 && second_code == 0, "Parallel launch failed");
    expect_equal(read_file(state / "disk_monitor.pid"), process_record(daemon.id()));
    int alive = 0;
    for (const auto& entry : fs::directory_iterator("/proc")) {
        std::error_code error;
        const auto executable = fs::read_symlink(entry.path() / "exe", error);
        if (!error && executable == binary) {
            const auto fields = process_fields(static_cast<pid_t>(std::stol(entry.path().filename().string())));
            if (fields[1] == std::to_string(getpid())) {
                ++alive;
            }
        }
    }
    expect(alive == 1, "Parallel launch left multiple daemons");
    expect(kill(daemon.id(), SIGTERM) == 0, "Cannot stop parallel daemon");
    expect(daemon.wait() == 0, "Parallel daemon did not stop normally");
    expect(!fs::exists(state / "disk_monitor.pid"), "Parallel daemon left a PID file");
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0) {
    }
    return "оба параллельных запуска вернули код 0; остался ровно один демон, PID верен; SIGTERM убрал его PID-файл";
}

std::string daemon_message(const Monitor& monitor, const std::string& message) {
    return "disk_monitor[" + std::to_string(monitor.process_id()) + "]: " + message;
}

void request_reload(Monitor& monitor, const fs::path& config, LogTest& logs) {
    const auto offset = fs::file_size(logs.system_log);
    monitor.signal(SIGHUP);
    wait_for(logs.system_log, offset,
             {daemon_message(monitor, "Config reloaded: " + config.string())});
    monitor.check_running();
}

std::string test_reload_directories() {
    ConfigTest config;
    LogTest logs;
    const auto keep = config.temp.path / "keep";
    const auto added = config.temp.path / "новая папка";
    fs::create_directory(keep);
    fs::create_directory(added);
    write_file(config.config, "paths: [data, keep]\n");
    Monitor monitor("config.yaml", {}, config.temp.path);
    const auto record = read_file(monitor.pid_path());
    write_file(config.config, "paths: ['новая папка', keep, './новая папка']\n");
    request_reload(monitor, config.config, logs);
    const auto offset = fs::file_size(logs.file_log);
    write_file(config.data / "removed.txt", "data");
    write_file(added / "added.txt", "data");
    write_file(keep / "kept.txt", "data");
    const auto text = wait_for(logs.file_log, offset,
                              {event_line(added / "added.txt", "IN_CLOSE_WRITE"),
                               event_line(keep / "kept.txt", "IN_CLOSE_WRITE")});
    expect_absent(text, (config.data / "removed.txt").string());
    expect_equal(read_file(monitor.pid_path()), record);
    return "новая папка и сохранённая папка наблюдаются, исключённая — нет; относительные пути и кириллица работают, PID не изменился";
}

std::string test_reload_invalid_config() {
    ConfigTest config;
    LogTest logs;
    const auto added = config.temp.path / "added";
    fs::create_directory(added);
    write_file(config.temp.path / "file.txt", "data");
    write_file(config.config, "paths: [data]\n");
    Monitor monitor(config.config);
    const auto record = read_file(monitor.pid_path());
    const std::vector<std::string> cases = {
        "paths: [\n", "paths: []\n", "paths: [missing]\n", "paths: [file.txt]\n",
        "paths: [added, missing]\n"
    };
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const auto service_offset = fs::file_size(logs.system_log);
        const auto event_offset = fs::file_size(logs.file_log);
        write_file(config.config, cases[i]);
        monitor.signal(SIGHUP);
        wait_for(logs.system_log, service_offset, {daemon_message(monitor, "Reload failed:")});
        monitor.check_running();
        const auto name = "old-" + std::to_string(i) + ".txt";
        write_file(added / name, "data");
        write_file(config.data / name, "data");
        const auto text = wait_for(logs.file_log, event_offset,
                                  {event_line(config.data / name, "IN_CLOSE_WRITE")});
        expect_absent(text, (added / name).string());
        expect_equal(read_file(monitor.pid_path()), record);
    }
    write_file(config.config, "paths: [added]\n");
    request_reload(monitor, config.config, logs);
    write_file(added / "recovered.txt", "data");
    wait_for(logs.file_log, logs.file_offset, {event_line(added / "recovered.txt", "IN_CREATE")});
    return "все 5 неверных конфигов записали ошибку и сохранили прежнее наблюдение и PID; частичный набор не принят, исправленный конфиг принят";
}

std::string test_reload_permissions() {
    ConfigTest config;
    LogTest logs;
    const auto added = config.temp.path / "added";
    fs::create_directory(added);
    write_file(config.config, "paths: [data]\n");
    Monitor monitor(config.config);
    for (const auto& path : {config.config, added}) {
        write_file(config.config, "paths: [added]\n");
        const auto offset = fs::file_size(logs.system_log);
        {
            Permissions permissions(path, fs::perms::none);
            monitor.signal(SIGHUP);
            wait_for(logs.system_log, offset, {daemon_message(monitor, "Reload failed:")});
        }
        monitor.check_running();
        const auto name = path == added ? "directory-error.txt" : "config-error.txt";
        write_file(config.data / name, "data");
        wait_for(logs.file_log, logs.file_offset, {event_line(config.data / name, "IN_CREATE")});
    }
    request_reload(monitor, config.config, logs);
    write_file(added / "accessible.txt", "data");
    wait_for(logs.file_log, logs.file_offset, {event_line(added / "accessible.txt", "IN_CREATE")});
    return "конфиг 000 и новая папка 000 отклонены; прежняя папка наблюдается; после восстановления прав SIGHUP успешен";
}

std::string test_reload_config_file_types() {
    ConfigTest config;
    LogTest logs;
    write_file(config.config, "paths: [data]\n");
    Monitor monitor(config.config);
    fs::remove(config.config);
    for (bool fifo : {false, true}) {
        const auto offset = fs::file_size(logs.system_log);
        if (fifo) {
            expect(mkfifo(config.config.c_str(), 0600) == 0, "Cannot create config FIFO");
        }
        monitor.signal(SIGHUP);
        wait_for(logs.system_log, offset, {daemon_message(monitor, "Reload failed:")});
        monitor.check_running();
        const auto name = fifo ? "fifo.txt" : "missing-config.txt";
        write_file(config.data / name, "data");
        wait_for(logs.file_log, logs.file_offset, {event_line(config.data / name, "IN_CREATE")});
    }
    fs::remove(config.config);
    write_file(config.config, "paths: [data]\n");
    request_reload(monitor, config.config, logs);
    return "удалённый конфиг и FIFO вместо YAML не остановили и не заблокировали демон; восстановленный файл перечитан";
}

std::string test_reload_restores_directory() {
    ConfigTest config;
    LogTest logs;
    write_file(config.config, "paths: [data]\n");
    Monitor monitor(config.config);
    expect(fs::remove(config.data), "Cannot remove watched directory");
    wait_for(logs.system_log, logs.system_offset,
             {daemon_message(monitor, "Watch lost: " + show_text(config.data.string()))});
    fs::create_directory(config.data);
    request_reload(monitor, config.config, logs);
    write_file(config.data / "restored.txt", "data");
    wait_for(logs.file_log, logs.file_offset, {event_line(config.data / "restored.txt", "IN_CREATE")});
    return "после удаления и создания папки на прежнем пути SIGHUP восстановил наблюдение без перезапуска демона";
}

std::string test_reload_queued_events() {
    ConfigTest config;
    LogTest logs;
    const auto added = config.temp.path / "added";
    fs::create_directory(added);
    write_file(config.config, "paths: [data]\n");
    Monitor monitor(config.config);
    monitor.suspend();
    std::vector<std::string> messages;
    for (int i = 0; i < 800; ++i) {
        const auto file = config.data / ("queued-" + std::to_string(i) + ".txt");
        write_file(file, "data");
        messages.push_back(event_line(file, "IN_CREATE"));
        messages.push_back(event_line(file, "IN_CLOSE_WRITE"));
    }
    write_file(config.config, "paths: [added]\n");
    monitor.signal(SIGHUP);
    monitor.signal(SIGCONT);
    wait_for(logs.system_log, logs.system_offset,
             {daemon_message(monitor, "Config reloaded: " + config.config.string())});
    wait_for(logs.file_log, logs.file_offset, messages);
    const auto offset = fs::file_size(logs.file_log);
    write_file(config.data / "after-removal.txt", "data");
    write_file(added / "after-reload.txt", "data");
    const auto text = wait_for(logs.file_log, offset,
                              {event_line(added / "after-reload.txt", "IN_CLOSE_WRITE")});
    expect_absent(text, (config.data / "after-removal.txt").string());
    return "очередь больше одного буфера: create и close_write всех 800 файлов старой папки сохранены; после смены наблюдается новая папка";
}

std::pair<int, int> watcher_resources(pid_t pid) {
    int descriptors = 0;
    int watches = 0;
    const auto proc = fs::path("/proc") / std::to_string(pid);
    for (const auto& entry : fs::directory_iterator(proc / "fd")) {
        ++descriptors;
        if (fs::read_symlink(entry.path()) == "anon_inode:inotify") {
            std::istringstream info(read_file(proc / "fdinfo" / entry.path().filename()));
            std::string line;
            while (std::getline(info, line)) {
                if (line.find("inotify wd:") == 0) {
                    ++watches;
                }
            }
        }
    }
    return {descriptors, watches};
}

std::string test_repeated_reload() {
    ConfigTest config;
    LogTest logs;
    const auto added = config.temp.path / "added";
    fs::create_directory(added);
    write_file(config.config, "paths: [data]\n");
    Monitor monitor(config.config);
    const auto record = read_file(monitor.pid_path());
    const auto before = watcher_resources(monitor.process_id());
    expect(before.second == 1, "Expected one initial watch");
    for (int i = 0; i < 4; ++i) {
        const auto name = i % 2 == 0 ? "added" : "data";
        write_file(config.config, "paths: [" + std::string(name) + ", ./" + name + "]\n");
        request_reload(monitor, config.config, logs);
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (watcher_resources(monitor.process_id()) != before &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(10ms);
        }
        expect(watcher_resources(monitor.process_id()) == before, "Reload leaked descriptors or watches");
        expect_equal(read_file(monitor.pid_path()), record);
        const auto path = config.temp.path / name / ("round-" + std::to_string(i) + ".txt");
        write_file(path, "data");
        wait_for(logs.file_log, logs.file_offset, {event_line(path, "IN_CREATE")});
    }
    return "4 последовательных SIGHUP сменили каталоги; число fd и watches не выросло, дубликаты путей объединены, PID прежний";
}

std::string test_termination_priority() {
    ConfigTest config;
    LogTest logs;
    write_file(config.config, "paths: [data]\n");
    Monitor monitor(config.config);
    const auto stopped = daemon_message(monitor, "Monitoring stopped");
    const auto reloaded = daemon_message(monitor, "Config reloaded:");
    const auto failed = daemon_message(monitor, "Reload failed:");
    monitor.suspend();
    write_file(config.config, "paths: [missing]\n");
    monitor.signal(SIGHUP);
    monitor.signal(SIGTERM);
    monitor.signal(SIGCONT);
    expect(monitor.wait_for_exit() == 0, "Daemon did not stop normally");
    expect(!fs::exists(monitor.pid_path()), "Daemon left a PID file");
    const auto service = wait_for(logs.system_log, logs.system_offset, {stopped});
    expect_absent(service, reloaded);
    expect_absent(service, failed);
    return "при ожидающих SIGHUP и SIGTERM завершение получило приоритет: код 0, PID убран, конфиг не перечитывался";
}

std::string test_termination_under_load() {
    ConfigTest config;
    LogTest logs;
    write_file(config.config, "paths: [data]\n");
    Monitor monitor(config.config);
    const auto stopped = daemon_message(monitor, "Monitoring stopped");
    const pid_t pid = fork();
    if (pid == 0) {
        try {
            const auto deadline = std::chrono::steady_clock::now() + 10s;
            while (std::chrono::steady_clock::now() < deadline) {
                write_file(config.data / "busy.txt", "data");
                fs::remove(config.data / "busy.txt");
            }
            _exit(0);
        } catch (...) {
            _exit(1);
        }
    }
    Child writer(pid);
    wait_for(logs.file_log, logs.file_offset, {event_line(config.data / "busy.txt", "IN_CREATE")});
    expect(writer.running(), "Event producer stopped before SIGTERM");
    monitor.signal(SIGTERM);
    expect(monitor.wait_for_exit() == 0, "Daemon did not stop normally under load");
    expect(!fs::exists(monitor.pid_path()), "Daemon left a PID file under load");
    wait_for(logs.system_log, logs.system_offset, {stopped});
    return "при непрерывном создании, записи и удалении файлов демон завершился с кодом 0; "
           "завершение записано, PID убран";
}

std::string test_observation_scope() {
    ConfigTest config;
    LogTest logs;
    const auto nested = config.data / "nested";
    fs::create_directory(nested);
    write_file(config.config, "paths: [data]\n");
    Monitor monitor(config.config);
    const auto record = read_file(monitor.pid_path());
    write_file(nested / "hidden-before.txt", "data");
    fs::create_directory(config.data / "new");
    write_file(config.data / "new" / "hidden-new.txt", "data");
    write_file(config.data / "before-marker.txt", "data");
    auto text = wait_for(logs.file_log, logs.file_offset,
                         {event_line(config.data / "before-marker.txt", "IN_CLOSE_WRITE")});
    expect_absent(text, (nested / "hidden-before.txt").string());
    expect_absent(text, "hidden-new.txt");
    write_file(config.config, "paths: [data, data/nested]\n");
    request_reload(monitor, config.config, logs);
    write_file(nested / "visible.txt", "data");
    wait_for(logs.file_log, logs.file_offset, {event_line(nested / "visible.txt", "IN_CLOSE_WRITE")});
    write_file(config.config, "paths: [data]\n");
    request_reload(monitor, config.config, logs);
    const auto offset = fs::file_size(logs.file_log);
    write_file(nested / "hidden-after.txt", "data");
    write_file(config.data / "after-marker.txt", "data");
    text = wait_for(logs.file_log, offset, {event_line(config.data / "after-marker.txt", "IN_CLOSE_WRITE")});
    expect_absent(text, (nested / "hidden-after.txt").string());
    expect_equal(read_file(monitor.pid_path()), record);
    return "автоматической рекурсии нет для старой и новой подпапок; подпапка сначала скрыта, после добавления её пути и SIGHUP наблюдается, после исключения снова скрыта; PID прежний";
}

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: run_tests <disk_monitor> [test]\n";
        return EXIT_FAILURE;
    }

    try {
        expect(geteuid() != 0, "Run without sudo (adm group required)");
        expect(prctl(PR_SET_CHILD_SUBREAPER, 1) == 0, "Cannot collect test daemons");
        expect(access(argv[1], X_OK) == 0, "Run ./build.sh first");
        binary = fs::canonical(argv[1]);
        if (const auto path = std::getenv("OS_LAB_SANITIZER_DIR")) {
            sanitizer_dir = path;
            expect(fs::is_directory(sanitizer_dir), "Sanitizer report directory is missing");
        }

        const struct {
            const char* name;
            const char* title;
            std::string (*run)();
        } tests[] = {
            {"build", "Чистая сборка, повторная сборка и ошибки", test_build},
            {"invalid_arguments", "Справка и неверные аргументы", test_invalid_arguments},
            {"config_paths", "Пути конфигурации", test_config_paths},
            {"config_errors", "Ошибки конфигурации и тип файла", test_config_errors},
            {"permissions", "Права на конфигурацию и каталоги", test_permissions},
            {"daemon_environment", "Фоновый запуск и окружение демона", test_daemon_environment},
            {"closed_standard_input", "Запуск с закрытым stdin", test_closed_standard_input},
            {"daemon_stop", "Завершение демона и уборка PID", test_daemon_stop},
            {"daemon_restart", "Повторный запуск и проверка нового конфига", test_daemon_restart},
            {"invalid_pid_records", "Пустые, повреждённые и устаревшие PID", test_invalid_pid_records},
            {"foreign_pid", "PID постороннего процесса", test_foreign_pid},
            {"pid_identity", "Несовпадение времени старта и исполняемого файла", test_pid_identity},
            {"replaced_executable", "Повторный запуск после замены бинарника", test_replaced_executable},
            {"changed_pid_cleanup", "Уборка после внешнего изменения PID-файла", test_changed_pid_cleanup},
            {"state_file_types", "FIFO и ссылки вместо служебных файлов", test_state_file_types},
            {"state_permissions", "Права доступа к служебным файлам", test_state_permissions},
            {"stop_timeout", "Ограничение времени остановки прежнего демона", test_stop_timeout},
            {"lock_timeout", "Ограничение ожидания занятой блокировки", test_lock_timeout},
            {"parallel_start", "Два одновременных запуска", test_parallel_start},
            {"reload_directories", "Смена каталогов по SIGHUP", test_reload_directories},
            {"reload_invalid_config", "Ошибки конфигурации при SIGHUP", test_reload_invalid_config},
            {"reload_permissions", "Отказ доступа при перечитывании конфигурации", test_reload_permissions},
            {"reload_config_file_types", "Удалённый конфиг и FIFO при SIGHUP", test_reload_config_file_types},
            {"reload_restores_directory", "Восстановление потерянного наблюдения",
             test_reload_restores_directory},
            {"reload_queued_events", "Очередь событий перед сменой конфигурации", test_reload_queued_events},
            {"repeated_reload", "Повторные SIGHUP и ресурсы", test_repeated_reload},
            {"termination_priority", "Приоритет SIGTERM над SIGHUP", test_termination_priority},
            {"termination_under_load", "SIGTERM при непрерывных файловых событиях",
             test_termination_under_load},
            {"observation_scope", "Область наблюдения и подпапки", test_observation_scope},
            {"logging", "Журналы, фильтр и ошибки", test_logging},
            {"file_operations", "Операции с файлами и папками", test_file_operations},
            {"move_cookie", "Переименование и перенос файлов", test_move_cookie},
            {"special_filenames", "Имена файлов со специальными символами", test_special_filenames},
            {"batch_events", "Пакет событий и длинные имена", test_batch_events},
            {"deleted_directory", "Удаление наблюдаемой папки", test_deleted_directory},
            {"moved_directory", "Перенос наблюдаемой папки", test_moved_directory},
            {"moved_parent_directory", "Перенос родителя наблюдаемой подпапки", test_moved_parent_directory},
            {"events_before_directory_removal", "Очередь событий перед удалением каталогов",
             test_events_before_directory_removal},
            {"event_buffers", "Разбор буферов и масок inotify", test_event_buffers},
            {"overflow_recovery", "Переполнение после смены наблюдений", test_overflow_recovery},
            {"unmount_and_ignored", "Потеря наблюдения и размонтирование", test_unmount_and_ignored},
            {"watch_resources", "Флаги и освобождение дескрипторов", test_watch_resources},
        };

        const auto selected = std::count_if(std::begin(tests), std::end(tests), [&](const auto& test) {
            return argc != 3 || std::string(argv[2]) == test.name;
        });
        expect(selected != 0, "Unknown test: " + std::string(argc == 3 ? argv[2] : ""));
        std::cout << "Проверки Disk monitor\n\n";
        int total = 0;
        int failed = 0;
        for (const auto& test : tests) {
            if (argc == 3 && std::string(argv[2]) != test.name) {
                continue;
            }
            ++total;
            std::cout << '[' << total << '/' << selected << "] " << test.title << '\n' << std::flush;
            try {
                const auto result = test.run();
                check_sanitizer_reports();
                std::cout << "  Получено: " << result << "\n  Результат: пройдено";
            } catch (const std::exception& error) {
                ++failed;
                std::cout << "  Получено:\n";
                std::istringstream lines(error.what());
                std::string line;
                while (std::getline(lines, line)) {
                    std::cout << "    " << line << '\n';
                }
                try {
                    check_sanitizer_reports();
                } catch (const std::exception& report) {
                    std::cout << "    " << report.what() << '\n';
                }
                std::cout << "  Результат: не пройдено";
            }
            std::cout << "\n\n";
        }
        std::cout << "Всего: " << total << ". Пройдено: " << total - failed
                  << ". Ошибок: " << failed << ".\n";
        return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
