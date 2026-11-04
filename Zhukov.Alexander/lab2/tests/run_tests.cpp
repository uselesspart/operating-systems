#include "chat_window.h"
#include "session.h"
#include "arguments.h"
#include "chat.h"
#include "worker.h"

#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QSignalSpy>
#include <algorithm>
#include <filesystem>
#include <fcntl.h>
#include <functional>
#include <future>
#include <iostream>
#include <signal.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/socket.h>
#include <mqueue.h>
#include <sys/un.h>
#include <cstring>
#include <thread>
#include <tuple>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

const std::string type = make_connection()->type();

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::size_t descriptor_count() {
    auto directory = fs::path("/proc") / std::to_string(getpid()) / "fd";
    return static_cast<std::size_t>(std::distance(fs::directory_iterator(directory), fs::directory_iterator()));
}

QByteArray read_file(const fs::path& path) {
    QFile file(QString::fromStdString(path.string()));
    expect(file.open(QIODevice::ReadOnly), "Cannot read: " + path.string());
    return file.readAll();
}

void write_file(const fs::path& path, const QByteArray& text) {
    QFile file(QString::fromStdString(path.string()));
    expect(file.open(QIODevice::WriteOnly), "Cannot write: " + path.string());
    expect(file.write(text) == text.size(), "Cannot write: " + path.string());
    file.close();
    expect(file.error() == QFileDevice::NoError, "Cannot close: " + path.string());
}

struct Result {
    int code;
    QByteArray output;
    QByteArray error;
};

Result run(const QString& program, const QStringList& args, const fs::path& directory,
           const fs::path& temporary = {}) {
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    if (!temporary.empty()) {
        environment.insert("TMPDIR", QString::fromStdString(temporary.string()));
    }
    process.setProcessEnvironment(environment);
    process.setWorkingDirectory(QString::fromStdString(directory.string()));
    process.setChildProcessModifier([] { setpgid(0, 0); });
    process.start(program, args);
    expect(process.waitForStarted(5000), "Cannot start: " + program.toStdString());
    pid_t pid = static_cast<pid_t>(process.processId());
    if (!process.waitForFinished(120000)) {
        kill(-pid, SIGKILL);
        process.waitForFinished(5000);
        throw std::runtime_error("Command timeout: " + program.toStdString());
    }
    int code = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    return {code, process.readAllStandardOutput(), process.readAllStandardError()};
}

void success(const Result& result) {
    expect(result.code == 0,
           "Exit code: " + std::to_string(result.code) + "\nstdout:\n" +
               result.output.right(5000).toStdString() + "\nstderr:\n" +
               result.error.right(5000).toStdString());
}

class Project {
public:
    explicit Project(const fs::path& source) : temporary("/tmp/os-chat-test-XXXXXX") {
        expect(temporary.isValid(), "Cannot create test directory");
        root = fs::path(temporary.path().toStdString()) / "project with spaces";
        temp = fs::path(temporary.path().toStdString()) / "tmp";
        fs::create_directory(root);
        fs::create_directory(temp);
        for (const auto* name : {"CMakeLists.txt", "Makefile", "build.sh", "include", "src"}) {
            fs::copy(source / name, root / name, fs::copy_options::recursive);
        }
    }

    Result command(const QString& program, const QStringList& args = {}) const {
        return run(program, args, root, temp);
    }

    void check_bin(const std::string& type) const {
        for (const auto* role : {"host_", "client_"}) {
            fs::path path = root / "bin" / (role + type);
            expect(fs::is_regular_file(path) && access(path.c_str(), X_OK) == 0,
                   "Missing executable: " + path.string());
            success(command(QString::fromStdString(path.string()), {"--help"}));
        }
    }

    void check_cleanup() const {
        expect(fs::is_empty(temp), "Temporary build remains");
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            const auto name = entry.path().filename().string();
            expect(name != "CMakeCache.txt" && name != "CMakeFiles" &&
                       entry.path().extension() != ".o",
                   "Build file remains: " + entry.path().string());
        }
    }

    fs::path root;
    fs::path temp;

private:
    QTemporaryDir temporary;
};

std::string build_commands(const fs::path& source) {
    Project project(source);
    success(project.command("make"));
    project.check_bin("fifo");
    project.check_bin("sock");
    project.check_bin("mq");
    project.check_cleanup();
    success(project.command("make"));
    project.check_bin("fifo");
    project.check_bin("sock");
    project.check_bin("mq");
    project.check_cleanup();

    Project clean(source);
    success(clean.command("bash", {"./build.sh"}));
    clean.check_bin("fifo");
    clean.check_bin("sock");
    clean.check_bin("mq");
    clean.check_cleanup();
    return "make и build.sh: код 0";
}

std::string automatic_sources(const fs::path& source) {
    Project project(source);
    const auto build = QString::fromStdString((project.temp / "build").string());
    success(project.command("cmake", {"-S", ".", "-B", build}));
    success(project.command("cmake", {"--build", build, "--parallel", "2"}));
    const auto cmake = read_file(project.root / "CMakeLists.txt");
    write_file(project.root / "src" / "extra.cpp", "int extra_cpp() { return 1; }\n");
    write_file(project.root / "src" / "extra.c", "int extra_c(void) { return 2; }\n");
    auto probe = read_file(project.root / "src" / "conn_fifo.cpp");
    probe.replace("\"fifo\"", "\"probe\"");
    write_file(project.root / "src" / "conn_probe.cpp", probe);
    success(project.command("cmake", {"--build", build, "--parallel", "2"}));
    expect(read_file(project.root / "CMakeLists.txt") == cmake, "CMakeLists changed");

    for (const auto* name : {"host_fifo", "client_fifo", "host_sock", "client_sock", "host_mq", "client_mq", "host_probe", "client_probe"}) {
        fs::path path = project.temp / "build" / "bin" / name;
        expect(fs::is_regular_file(path), std::string("Missing target: ") + name);
        success(project.command(QString::fromStdString(path.string()), {"--help"}));
    }

    auto document = QJsonDocument::fromJson(read_file(project.temp / "build" /
                                                    "compile_commands.json"));
    expect(document.isArray(), "Invalid compile commands");
    bool found_cpp = false;
    bool found_c = false;
    bool found_probe = false;
    for (const auto& value : document.array()) {
        auto object = value.toObject();
        auto file = object["file"].toString();
        if (!file.startsWith(QString::fromStdString((project.root / "src").string()))) {
            continue;
        }
        auto command = object["command"].toString();
        expect(command.contains("-Wall") && command.contains("-Werror"),
               "Missing compiler flags: " + file.toStdString());
        if (file.endsWith(".cpp")) {
            expect(command.contains("-std=c++17"), "Missing C++17: " + file.toStdString());
        }
        found_cpp = found_cpp || file.endsWith("/extra.cpp");
        found_c = found_c || file.endsWith("/extra.c");
        found_probe = found_probe || file.endsWith("/conn_probe.cpp");
    }
    expect(found_cpp && found_c && found_probe, "New sources were not compiled");
    return "Новые исходники и цели собраны";
}

std::string build_failure(const fs::path& source) {
    Project project(source);
    success(project.command("bash", {"./build.sh"}));
    auto original = read_file(project.root / "bin" / "host_fifo");
    write_file(project.root / "src" / "broken.cpp", "int broken( {\n");
    auto result = project.command("bash", {"./build.sh"});
    expect(result.code != 0, "Invalid source compiled successfully");
    expect(!result.error.isEmpty(), "Compiler error was not reported");
    expect(!result.output.contains("Built:"), "Failed build reported success");
    expect(read_file(project.root / "bin" / "host_fifo") == original,
           "Failed build replaced executable");
    project.check_cleanup();
    return "Ошибка обработана, временные файлы удалены";
}

std::string arguments(const fs::path& bin, const fs::path& source) {
    for (const auto& role : {"host_" + type, "client_" + type}) {
        auto result = run(QString::fromStdString((bin / role).string()), {"--help"}, source);
        success(result);
        expect(result.output.contains("Usage:"), "Help is missing");
    }
    QStringList invalid{"0", "-1", "abc", "12x", "1.5", "", " 12", "999999999999999999999"};
    int checked = 0;
    for (const auto& text : invalid) {
        auto result = run(QString::fromStdString((bin / ("client_" + type)).string()), {text}, source);
        expect(result.code == 1 && result.error.contains("Invalid PID"),
               "Invalid PID accepted: " + text.toStdString());
        ++checked;
    }
    for (const QStringList& args : {QStringList{}, QStringList{"12", "34"}}) {
        auto result = run(QString::fromStdString((bin / ("client_" + type)).string()), args, source);
        expect(result.code == 1 && result.error.contains("Usage:"), "Invalid client arguments");
        ++checked;
    }
    auto result = run(QString::fromStdString((bin / ("host_" + type)).string()), {"12"}, source);
    expect(result.code == 1 && result.error.contains("Usage:"), "Invalid host arguments");
    return "Отклонено неверных запусков: " + std::to_string(checked + 1);
}

std::string windows(QApplication& app) {
    for (const auto* role : {"Host", "Client"}) {
        ChatWindow window(role, QString::fromStdString(type), 12345);
        auto label = [&window](const char* name) {
            auto* widget = window.findChild<QLabel*>(name);
            expect(widget != nullptr, std::string("Missing label: ") + name);
            return widget->text();
        };
        expect(label("role") == role && label("type") == QString::fromStdString(type) && label("host_pid") == "12345",
               "Invalid window labels");
        expect(label("status") == "Not connected", "Incorrect initial status");
        expect(QString(window.metaObject()->className()) == "ChatWindow", "AUTOMOC is missing");
        auto* send = window.findChild<QPushButton*>("send");
        auto* text = window.findChild<QLineEdit*>("text");
        auto* recipient = window.findChild<QComboBox*>("recipient");
        auto* history = window.findChild<QListWidget*>("history");
        expect(send && text && recipient && history, "Missing chat controls");
        expect(!send->isEnabled() && !text->isEnabled() && !recipient->isEnabled() &&
                   history->count() == 0,
               "Input enabled before connection");
        window.show();
        expect(QTest::qWaitForWindowExposed(&window, 3000), "Window not exposed");
        QTimer::singleShot(100, &window, &QWidget::close);
        expect(app.exec() == 0 && !window.isVisible(), "Window did not close");
    }
    return "Оба окна открылись и закрылись";
}

template <typename Action>
std::string failure(Action action) {
    try {
        action();
    } catch (const std::exception& error) {
        return error.what();
    }
    throw std::runtime_error("Expected an error");
}

class Child {
public:
    Child(const fs::path& program, const QStringList& args) {
        process.start(QString::fromStdString(program.string()), args);
        expect(process.waitForStarted(5000), "Cannot start child");
    }
    ~Child() {
        if (process.state() != QProcess::NotRunning) {
            process.terminate();
            if (!process.waitForFinished(1500)) {
                process.kill();
                process.waitForFinished(5000);
            }
        }
    }
    pid_t pid() const { return static_cast<pid_t>(process.processId()); }
    Result finish(int wait_ms = 10000) {
        expect(process.state() == QProcess::NotRunning || process.waitForFinished(wait_ms), "Child timeout");
        return {process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1,
                process.readAllStandardOutput(), process.readAllStandardError()};
    }
    QProcess process;
};

ReadResult receive_packet(Session& session, Message& message, const std::atomic_bool& stop) {
    auto deadline = Clock::now() + std::chrono::seconds(7);
    while (Clock::now() < deadline) {
        auto result = session.receive(message, stop);
        if (result != ReadResult::NoData) {
            return result;
        }
    }
    throw std::runtime_error("Receive timeout");
}

void accept_peer(Session& session, const std::atomic_bool& stop) {
    auto deadline = Clock::now() + std::chrono::seconds(5);
    while (Clock::now() < deadline) {
        if (session.accept(stop)) {
            return;
        }
    }
    throw std::runtime_error("Handshake timeout");
}

Bytes frame(const Bytes& packet) {
    Bytes bytes;
    for (unsigned i = 0; i < 4; ++i) {
        bytes.push_back(static_cast<char>(packet.size() >> (i * 8)));
    }
    bytes.insert(bytes.end(), packet.begin(), packet.end());
    return bytes;
}

void raw_write(int fd, const Bytes& bytes) {
    expect(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()),
           "Cannot write test bytes");
}

Fd connect_socket(const std::string& directory) {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    auto path = directory + "/socket";
    expect(path.size() < sizeof(address.sun_path), "Test socket path too long");
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    Fd fd(socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
    expect(fd.get() >= 0 && connect(fd.get(), reinterpret_cast<sockaddr*>(&address),
                                  sizeof(address)) == 0, "Cannot connect test socket");
    return fd;
}

class Queue {
public:
    Queue(const std::string& name, bool create = false, long count = 10, long size = max_packet)
        : name(name), owner(create) {
        mq_attr attributes{};
        attributes.mq_maxmsg = count;
        attributes.mq_msgsize = size;
        int flags = O_RDWR | O_NONBLOCK | O_CLOEXEC;
        value = create ? mq_open(name.c_str(), flags | O_CREAT | O_EXCL, 0600, &attributes)
                       : mq_open(name.c_str(), flags);
        expect(value != static_cast<mqd_t>(-1), "Cannot open test queue");
    }
    ~Queue() {
        mq_close(value);
        if (owner) { mq_unlink(name.c_str()); }
    }
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;
    mqd_t get() const { return value; }
    mq_attr attributes() const {
        mq_attr result{};
        expect(mq_getattr(value, &result) == 0, "Cannot read queue attributes");
        return result;
    }
    void send(const Bytes& bytes, unsigned priority = 0) {
        expect(mq_send(value, bytes.data(), bytes.size(), priority) == 0, "Cannot send test queue");
    }
    Bytes receive(unsigned* priority = nullptr) {
        Bytes bytes(attributes().mq_msgsize);
        ssize_t count = mq_receive(value, bytes.data(), bytes.size(), priority);
        expect(count >= 0, "Cannot receive test queue");
        bytes.resize(static_cast<std::size_t>(count));
        return bytes;
    }

private:
    std::string name;
    bool owner;
    mqd_t value;
};

bool queue_missing(const std::string& name) {
    mqd_t value = mq_open(name.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (value != static_cast<mqd_t>(-1)) {
        mq_close(value);
        return false;
    }
    expect(errno == ENOENT, "Cannot check queue cleanup");
    return true;
}

int raw_socket_peer(pid_t pid, const std::string& mode) {
    std::atomic_bool stop{false};
    auto directory = Session::path(pid, type);
    NamedSemaphore slot;
    slot.open(Session::semaphore_name(pid, type, "slot"), false);
    expect(slot.try_wait(), "Host busy");
    auto fd = connect_socket(directory);
    write_file(directory + "/request", QByteArray::number(getpid()));
    expect(chmod((directory + "/request").c_str(), 0600) == 0, "Cannot set request mode");
    NamedSemaphore accepted;
    accepted.open(Session::semaphore_name(pid, type, "accepted"), false);
    expect(kill(pid, SIGUSR1) == 0, "Cannot signal host");
    accepted.wait(Clock::now() + std::chrono::seconds(5), stop);
    Message message;
    message.sender = mode == "spoof" ? 0 : 1;
    message.text = "no token";
    auto bytes = frame(encode(message));
    if (mode == "unposted") {
        raw_write(fd.get(), Bytes(bytes.begin(), bytes.begin() + 2));
        std::this_thread::sleep_for(std::chrono::seconds(3));
        raw_write(fd.get(), Bytes(bytes.begin() + 2, bytes.end()));
        std::this_thread::sleep_for(std::chrono::seconds(7));
    } else {
        raw_write(fd.get(), bytes);
        NamedSemaphore ready;
        ready.open(Session::semaphore_name(pid, type, "up"), false);
        ready.post();
    }
    return 0;
}

int peer_main(pid_t pid, const std::string& mode) {
    try {
        if (type == "sock" && (mode == "unposted" || mode == "spoof")) {
            return raw_socket_peer(pid, mode);
        }
        std::atomic_bool stop{false};
        Session session(make_connection(), pid, false);
        session.connect(stop);
        Message message;
        if (mode.rfind("pair-", 0) == 0) {
            for (int i = 0; i < 8; ++i) {
                message.text = mode + std::to_string(i);
                auto sent = session.send(message, stop);
                auto result = receive_packet(session, message, stop);
                if (result == ReadResult::Closed) {
                    return 0;
                }
                expect(result == ReadResult::Data && message == sent, "Session messages crossed");
                if (i == 0) {
                    std::cout << "ready\n" << std::flush;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            return 0;
        }
        if (mode == "silent") {
            auto deadline = Clock::now() + std::chrono::seconds(90);
            message.kind = MessageKind::System;
            message.text = "ping";
            while (Clock::now() < deadline) {
                session.send(message, stop);
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            return 0;
        }
        if (mode == "roundtrip") {
            for (int i = 0; i < 32; ++i) {
                message.text = "client " + std::to_string(i);
                session.send(message, stop);
                expect(receive_packet(session, message, stop) == ReadResult::Data &&
                           message.text == "host " + std::to_string(i) && message.sender == 0,
                       "Wrong host message");
            }
            return 0;
        }
        if (mode == "unposted" || mode == "spoof") {
            message.sender = mode == "spoof" ? 0 : 1;
            message.text = "no token";
            if (type == "mq") {
                Queue queue(queue_name(Session::path(pid, type), "up"));
                queue.send(encode(message));
                if (mode == "unposted") {
                    std::this_thread::sleep_for(std::chrono::seconds(7));
                } else {
                    NamedSemaphore ready;
                    ready.open(Session::semaphore_name(pid, type, "up"), false);
                    ready.post();
                }
                return 0;
            }
            Fd fd(open((Session::path(pid, type) + "/up").c_str(), O_WRONLY | O_NONBLOCK));
            expect(fd.get() >= 0, "Cannot open raw FIFO");
            auto bytes = frame(encode(message));
            if (mode == "unposted") {
                raw_write(fd.get(), Bytes(bytes.begin(), bytes.begin() + 2));
                std::this_thread::sleep_for(std::chrono::seconds(3));
                raw_write(fd.get(), Bytes(bytes.begin() + 2, bytes.end()));
                std::this_thread::sleep_for(std::chrono::seconds(7));
            } else {
                raw_write(fd.get(), bytes);
                NamedSemaphore ready;
                ready.open(Session::semaphore_name(pid, type, "up"), false);
                ready.post();
            }
            return 0;
        }
        if (mode == "bye-burst") {
            for (int i = 1; i <= 3; ++i) {
                message.text = "last " + std::to_string(i);
                session.send(message, stop);
            }
            return 0;
        }
        message.text = mode == "bye" ? "last" : "ready";
        session.send(message, stop);
        if (mode == "bye") {
            return 0;
        }
        auto deadline = Clock::now() + std::chrono::seconds(30);
        while (Clock::now() < deadline) {
            if (session.receive(message, stop) == ReadResult::Data) {
                expect(message.text == "done", "Wrong completion message");
                return 0;
            }
        }
        throw std::runtime_error("Completion timeout");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

void check_resources_gone(pid_t pid, const std::string& transport = type) {
    expect(!fs::exists(Session::path(pid, transport)), "Session directory remains");
    for (const auto* suffix : {"slot", "accepted", "up", "down"}) {
        auto name = Session::semaphore_name(pid, transport, suffix);
        sem_t* sem = sem_open(name.c_str(), 0);
        if (sem != SEM_FAILED) {
            sem_close(sem);
            throw std::runtime_error("Semaphore remains: " + name);
        }
        expect(errno == ENOENT, "Cannot check semaphore cleanup");
    }
    if (transport == "mq") {
        for (const auto* suffix : {"up", "down"}) {
            expect(queue_missing(queue_name(Session::path(pid, transport), suffix)), "Queue remains");
        }
    }
}

std::string protocol() {
    Message message;
    message.sender = 1;
    message.sent_at = 123;
    message.number = 7;
    for (auto kind : {MessageKind::Public, MessageKind::Private, MessageKind::System}) {
        message.kind = kind;
        message.recipient = kind == MessageKind::Public ? -1 : 0;
        message.text = std::string(1020, 'a') + "😀";
        auto packet = encode(message);
        auto decoded = decode(packet);
        expect(decoded.text == message.text && decoded.kind == message.kind &&
                   decoded.sender == 1 && decoded.sent_at == 123 && decoded.number == 7,
               "Protocol round trip failed");
    }
    message.text = "Привет, <b>чат</b>!\n";
    auto packet = encode(message);
    expect(decode(packet).text == message.text, "UTF-8 changed");
    for (std::size_t size = 0; size < packet.size(); ++size) {
        failure([&] { decode(Bytes(packet.begin(), packet.begin() + size)); });
    }
    for (auto offset : {0, 4, 5, 6, 32}) {
        auto broken = packet;
        broken[offset] = static_cast<char>(255);
        failure([&] { decode(broken); });
    }
    auto extra = packet;
    extra.push_back('x');
    failure([&] { decode(extra); });
    message.text.assign(1025, 'x');
    failure([&] { encode(message); });
    for (const std::string& text : {std::string("\xff"), std::string("\xf0\x9f"),
                                    std::string("\xc0\x80")}) {
        message.text = text;
        failure([&] { encode(message); });
    }
    message.text = "private";
    message.kind = MessageKind::Private;
    message.recipient = -1;
    failure([&] { encode(message); });
    return "Пакеты и UTF-8 проверены";
}

std::string session_exchange(const fs::path& self) {
    std::atomic_bool stop{false};
    auto descriptors = descriptor_count();
    for (int cycle = 0; cycle < 3; ++cycle) {
        {
            Session session(make_connection(), getpid(), true);
            struct stat info{};
            auto path = Session::path(getpid(), type);
            expect(stat(path.c_str(), &info) == 0 && (info.st_mode & 0777) == 0700,
                   "Invalid directory permissions");
            std::vector<const char*> files{"ready"};
            if (type == "fifo") { files = {"up", "down", "ready"}; }
            if (type == "sock") { files = {"socket", "ready"}; }
            for (const auto* file : files) {
                expect(stat((path + "/" + file).c_str(), &info) == 0 && (info.st_mode & 0777) == 0600,
                       "Invalid resource permissions");
            }
            for (const auto* suffix : {"slot", "accepted", "up", "down"}) {
                auto name = Session::semaphore_name(getpid(), type, suffix);
                expect(stat(("/dev/shm/sem." + name.substr(1)).c_str(), &info) == 0 &&
                           (info.st_mode & 0777) == 0600, "Invalid semaphore permissions");
            }
            Child peer(self, {"--peer", QString::number(getpid()), "roundtrip"});
            pid_t pid = peer.pid();
            accept_peer(session, stop);
            expect(session.peer_pid() == pid, "Wrong signal sender");
            for (int i = 0; i < 32; ++i) {
                Message message;
                message.text = "host " + std::to_string(i);
                session.send(message, stop);
                expect(receive_packet(session, message, stop) == ReadResult::Data &&
                           message.text == "client " + std::to_string(i) && message.sender == 1,
                       "Wrong client message");
            }
            success(peer.finish());
            Message message;
            expect(receive_packet(session, message, stop) == ReadResult::Closed, "EOF missing");
        }
        check_resources_gone(getpid());
        expect(descriptor_count() == descriptors, "Session leaked descriptors");
    }
    {
        Session session(make_connection(), getpid(), true);
        Child peer(self, {"--peer", QString::number(getpid()), "spoof"});
        accept_peer(session, stop);
        Message message;
        expect(failure([&] { receive_packet(session, message, stop); }) == "Invalid sender",
               "Forged sender accepted");
        success(peer.finish());
    }
    check_resources_gone(getpid());
    return "3 цикла без роста дескрипторов";
}

std::string busy_and_idle(const fs::path& self) {
    std::atomic_bool stop{false};
    {
        Session session(make_connection(), getpid(), true);
        std::this_thread::sleep_for(std::chrono::seconds(6));
        Child peer(self, {"--peer", QString::number(getpid()), "hold"});
        accept_peer(session, stop);
        Message message;
        expect(receive_packet(session, message, stop) == ReadResult::Data && message.text == "ready",
               "Client did not become ready");
        auto result = run(QString::fromStdString(self.string()),
                          {"--peer", QString::number(getpid()), "hold"}, self.parent_path());
        expect(result.code == 1 && result.error.contains("Host busy"), "Second client admitted");
        auto deadline = Clock::now() + std::chrono::seconds(10);
        while (Clock::now() < deadline) {
            expect(session.receive(message, stop) == ReadResult::NoData, "Idle connection failed");
        }
        message.text = "done";
        session.send(message, stop);
        success(peer.finish());
    }
    check_resources_gone(getpid());
    return "Второй клиент отклонён, спокойная связь сохранена";
}

class Pair {
public:
    explicit Pair(bool raw = false) : directory("/tmp/os-chat-pair-XXXXXX"), host(make_connection()) {
        expect(directory.isValid(), "Cannot create pair directory");
        host->open(directory.path().toStdString(), true);
        if (type == "sock" && raw) {
            socket = connect_socket(directory.path().toStdString());
        } else {
            client = make_connection();
            client->open(directory.path().toStdString(), false);
        }
        host->accept();
    }
    Fd writer() const {
        if (type == "sock") {
            Fd fd(fcntl(socket.get(), F_DUPFD_CLOEXEC, 0));
            expect(fd.get() >= 0, "Cannot copy socket");
            return fd;
        }
        Fd fd(open((directory.path().toStdString() + "/up").c_str(), O_WRONLY | O_NONBLOCK));
        expect(fd.get() >= 0, "Cannot open raw FIFO");
        return fd;
    }
    void send(const Bytes& packet) {
        if (client) {
            client->send(packet, stop);
        } else {
            raw_write(socket.get(), frame(packet));
        }
    }
    void close_client() { client.reset(); socket.reset(); }
    QTemporaryDir directory;
    std::unique_ptr<Conn> host;
    std::unique_ptr<Conn> client;
    Fd socket;
    std::atomic_bool stop{false};
};

class DelayedConn final : public Conn {
public:
    explicit DelayedConn(std::atomic_bool* ready = nullptr) : ready(ready) {}
    std::string type() const override { return conn->type(); }
    void open(const std::string& directory, bool create) override { conn->open(directory, create); }
    void accept() override { conn->accept(); }
    bool writable() const override { return conn->writable(); }
    pid_t peer_pid() const override { return conn->peer_pid(); }
    void send(const Bytes& bytes, const std::atomic_bool& stop) override { conn->send(bytes, stop); }
    ReadResult receive(Bytes& bytes, const std::atomic_bool& stop, int wait_ms,
                       Deadline* deadline) override {
        if (first) {
            auto until = Clock::now() + std::chrono::seconds(5);
            while (ready && !ready->load()) {
                check_stop(stop);
                expect(Clock::now() < until, "Delayed read timeout");
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            first = false;
            return ReadResult::NoData;
        }
        return conn->receive(bytes, stop, wait_ms, deadline);
    }

private:
    std::unique_ptr<Conn> conn = make_connection();
    bool first = true;
    std::atomic_bool* ready;
};

std::string queue_packets(const fs::path& self) {
    {
        Pair pair;
        auto root = pair.directory.path().toStdString();
        Queue queue(queue_name(root, "up"));
        auto attributes = queue.attributes();
        struct stat info{};
        expect(attributes.mq_maxmsg == 10 && attributes.mq_msgsize == static_cast<long>(max_packet) &&
                   attributes.mq_curmsgs == 0 && (attributes.mq_flags & O_NONBLOCK) &&
                   fstat(static_cast<int>(queue.get()), &info) == 0 && info.st_uid == getuid() &&
                   (info.st_mode & 0777) == 0600, "Invalid queue attributes");
        Message message;
        message.text = std::string(1020, 'a') + "😀";
        for (int batch = 0; batch < 2; ++batch) {
            for (int i = 0; i < 10; ++i) {
                message.number = batch * 10 + i;
                pair.client->send(encode(message), pair.stop);
            }
            for (int i = 0; i < 10; ++i) {
                unsigned priority = 99;
                auto bytes = queue.receive(&priority);
                auto got = decode(bytes);
                expect(priority == 0 && bytes.size() == max_packet && got.text == message.text &&
                           got.number == static_cast<unsigned>(batch * 10 + i), "Queue order or bytes changed");
            }
        }
        auto packet = encode(message);
        pair.client->send(packet, pair.stop);
        pair.close_client();
        Bytes got;
        expect(pair.host->receive(got, pair.stop, 100) == ReadResult::Data && got == packet,
               "Last queue packet lost");
        expect(pair.host->receive(got, pair.stop, 20) == ReadResult::NoData, "Empty queue is not EOF");
        Bytes large(max_packet + 1, 'x');
        expect(mq_send(queue.get(), large.data(), large.size(), 0) == -1 && errno == EMSGSIZE,
               "Kernel accepted oversized queue packet");
        expect(failure([&] { pair.host->send(large, pair.stop); }) == "Invalid outgoing packet",
               "Transport accepted oversized queue packet");
        queue.send(Bytes{'x'});
        expect(failure([&] { pair.host->receive(got, pair.stop, 100); }) == "Invalid queue packet",
               "Truncated queue packet accepted");
        queue.send(packet, 1);
        expect(failure([&] { pair.host->receive(got, pair.stop, 100); }) == "Invalid queue packet",
               "Unexpected queue priority accepted");
    }
    {
        std::atomic_bool stop{false};
        Session session(std::make_unique<DelayedConn>(), getpid(), true);
        Child peer(self, {"--peer", QString::number(getpid()), "bye"});
        accept_peer(session, stop);
        success(peer.finish());
        Message message;
        expect(receive_packet(session, message, stop) == ReadResult::Data && message.text == "last",
               "Confirmed queue packet lost after peer exit");
        expect(receive_packet(session, message, stop) == ReadResult::Closed, "Queue peer exit missing");
    }
    check_resources_gone(getpid());
    return "Пакеты целые, порядок и размер сохранены";
}

std::string stream_packets(const fs::path& self) {
    if (type == "mq") { return queue_packets(self); }
    {
        Pair pair(true);
        Fd writer = pair.writer();
        Message message;
        message.text = "Привет";
        auto packet = encode(message);
        auto bytes = frame(packet);
        raw_write(writer.get(), Bytes(bytes.begin(), bytes.begin() + 2));
        Bytes got;
        expect(pair.host->receive(got, pair.stop, 20) == ReadResult::NoData, "Partial packet exposed");
        raw_write(writer.get(), Bytes(bytes.begin() + 2, bytes.end()));
        expect(pair.host->receive(got, pair.stop, 100) == ReadResult::Data && got == packet,
               "Fragmented packet lost");
        auto two = bytes;
        two.insert(two.end(), bytes.begin(), bytes.end());
        raw_write(writer.get(), two);
        for (int i = 0; i < 2; ++i) {
            expect(pair.host->receive(got, pair.stop, 100) == ReadResult::Data && got == packet,
                   "Packet boundaries lost");
        }
        pair.send(packet);
        writer.reset();
        pair.close_client();
        expect(pair.host->receive(got, pair.stop, 100) == ReadResult::Data && got == packet,
               "Last packet lost");
        expect(pair.host->receive(got, pair.stop, 100) == ReadResult::Closed, "EOF not detected");
    }
    {
        std::atomic_bool stop{false};
        Session session(make_connection(), getpid(), true);
        Child peer(self, {"--peer", QString::number(getpid()), "bye"});
        accept_peer(session, stop);
        success(peer.finish());
        Message message;
        expect(receive_packet(session, message, stop) == ReadResult::Data && message.text == "last",
               "Confirmed packet lost at EOF");
        expect(receive_packet(session, message, stop) == ReadResult::Closed, "Session EOF missing");
    }
    {
        Pair pair(true);
        auto writer = pair.writer();
        raw_write(writer.get(), Bytes{40, 0, 0, 0, 'x'});
        pair.close_client();
        writer.reset();
        Bytes got;
        expect(failure([&] { pair.host->receive(got, pair.stop, 100); }) == "Truncated packet",
               "Truncated packet accepted");
    }
    {
        Pair pair(true);
        auto writer = pair.writer();
        raw_write(writer.get(), Bytes{static_cast<char>(255), static_cast<char>(255),
                                      static_cast<char>(255), static_cast<char>(255)});
        Bytes got;
        expect(failure([&] { pair.host->receive(got, pair.stop, 100); }) == "Invalid frame size",
               "Oversized frame accepted");
    }
    check_resources_gone(getpid());
    return "Границы пакетов и последний пакет сохранены";
}

void harmless_signal(int) {}

std::string semaphore_deadlines(const fs::path& self, const fs::path& bin) {
    std::atomic_bool stop{false};
    {
        NamedSemaphore sem;
        auto name = "/os_lab2_test_" + std::to_string(getpid());
        sem.open(name, true);
        std::thread release([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            sem.post();
        });
        auto start = Clock::now();
        sem.wait(start + std::chrono::seconds(5), stop);
        release.join();
        expect(Clock::now() - start >= std::chrono::milliseconds(100), "Semaphore did not block");
        struct sigaction action{};
        action.sa_handler = harmless_signal;
        sigemptyset(&action.sa_mask);
        expect(sigaction(SIGUSR2, &action, nullptr) == 0, "Cannot install test signal");
        auto thread = pthread_self();
        std::thread interrupt([thread] {
            for (int i = 0; i < 15; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                pthread_kill(thread, SIGUSR2);
            }
        });
        start = Clock::now();
        auto error = failure([&] { sem.wait(start + std::chrono::seconds(5), stop); });
        interrupt.join();
        auto elapsed = Clock::now() - start;
        expect(error == "Semaphore timeout" && elapsed >= std::chrono::milliseconds(4900) &&
                   elapsed < std::chrono::seconds(7), "Semaphore deadline changed");
    }
    {
        Session session(make_connection(), getpid(), true);
        auto start = Clock::now();
        auto result = run(QString::fromStdString((bin / ("client_" + type)).string()),
                          {QString::number(getpid())}, bin);
        expect(result.code == 1 && result.error.contains("Semaphore timeout") &&
                   Clock::now() - start >= std::chrono::milliseconds(4900) &&
                   Clock::now() - start < std::chrono::seconds(7), "Handshake timeout failed");
    }
    {
        Session session(make_connection(), getpid(), true);
        Child peer(self, {"--peer", QString::number(getpid()), "unposted"});
        accept_peer(session, stop);
        Message message;
        auto start = Clock::now();
        auto error = failure([&] { receive_packet(session, message, stop); });
        expect(error == "Semaphore timeout" &&
                   Clock::now() - start >= std::chrono::milliseconds(4900) &&
                   Clock::now() - start < std::chrono::seconds(7),
               "Read and semaphore deadlines were added");
    }
    check_resources_gone(getpid());
    return "Семафоры и предел 5 секунд работают";
}

std::string socket_deadlines() {
    {
        Pair pair(true);
        auto writer = pair.writer();
        raw_write(writer.get(), Bytes{1, 0});
        Bytes packet;
        auto start = Clock::now();
        auto error = failure([&] {
            while (Clock::now() - start < std::chrono::seconds(7)) {
                pair.host->receive(packet, pair.stop, 100);
            }
        });
        expect(error == "Read timeout" && Clock::now() - start >= std::chrono::milliseconds(4900),
               "Partial socket deadline failed");
    }
    {
        Pair pair(true);
        Message message;
        message.text = std::string(max_text, 's');
        auto packet = encode(message);
        auto start = Clock::now();
        auto error = failure([&] {
            while (Clock::now() - start < std::chrono::seconds(7)) {
                pair.host->send(packet, pair.stop);
            }
        });
        expect(error == "Write timeout" && Clock::now() - start >= std::chrono::milliseconds(4900) &&
                   Clock::now() - start < std::chrono::seconds(7), "Full socket deadline failed");
        pair.stop.store(true);
        expect(failure([&] { pair.host->send(packet, pair.stop); }) == "Cancelled",
               "Socket write was not cancelled");
        expect(failure([&] { pair.host->receive(packet, pair.stop, 100); }) == "Cancelled",
               "Socket read was not cancelled");
    }
    {
        Pair pair(true);
        Message message;
        message.text = std::string(max_text, 'r');
        auto packet = encode(message);
        auto bytes = frame(packet);
        Bytes expected;
        for (int i = 0; i < 512; ++i) {
            expected.insert(expected.end(), bytes.begin(), bytes.end());
        }
        auto sender = std::async(std::launch::async, [&] {
            for (int i = 0; i < 512; ++i) {
                pair.host->send(packet, pair.stop);
            }
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        Bytes got;
        auto deadline = Clock::now() + std::chrono::seconds(7);
        while (got.size() < expected.size() && Clock::now() < deadline) {
            char buffer[8192];
            ssize_t count = read(pair.socket.get(), buffer, sizeof(buffer));
            if (count > 0) {
                got.insert(got.end(), buffer, buffer + count);
            } else if (count < 0 && (errno == EAGAIN || errno == EINTR)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            } else {
                throw std::runtime_error("Cannot drain socket");
            }
        }
        sender.get();
        expect(got == expected, "Socket bytes lost after drain");
    }
    return "Тайм-ауты, отмена и заполнение сокета проверены";
}

std::string queue_deadlines() {
    {
        Pair pair;
        Queue queue(queue_name(pair.directory.path().toStdString(), "up"));
        Message message;
        message.text = "full";
        auto packet = encode(message);
        for (int i = 0; i < 10; ++i) { pair.client->send(packet, pair.stop); }
        expect(!pair.client->writable(), "Full queue reported writable");
        auto release = std::async(std::launch::async, [&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            return queue.receive();
        });
        auto start = Clock::now();
        pair.client->send(packet, pair.stop);
        expect(release.get() == packet && Clock::now() - start >= std::chrono::milliseconds(100),
               "Full queue did not wait for space");
        expect(queue.attributes().mq_curmsgs == 10, "Queue capacity changed");
        auto error = failure([&] { pair.client->send(packet, pair.stop); });
        expect(error == "Write timeout" && Clock::now() - start >= std::chrono::milliseconds(5000) &&
                   Clock::now() - start < std::chrono::seconds(7), "Queue write deadline failed");
        pair.stop.store(true);
        expect(failure([&] { pair.client->send(packet, pair.stop); }) == "Cancelled",
               "Queue send was not cancelled");
        pair.stop.store(false);
        Bytes got;
        for (int i = 0; i < 10; ++i) {
            expect(pair.host->receive(got, pair.stop, 100) == ReadResult::Data && got == packet,
                   "Filled queue lost data");
        }
        expect(pair.client->writable(), "Drained queue is not writable");
    }
    {
        Pair pair;
        auto cancel = std::async(std::launch::async, [&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            pair.stop.store(true);
        });
        Bytes got;
        auto start = Clock::now();
        auto error = failure([&] { pair.host->receive(got, pair.stop, 5000); });
        cancel.get();
        expect(error == "Cancelled" && Clock::now() - start < std::chrono::seconds(2),
               "Queue receive cancellation blocked");
    }
    return "Заполнение, срок ожидания и отмена работают";
}

std::string stream_deadlines() {
    if (type == "mq") { return queue_deadlines(); }
    if (type == "sock") {
        return socket_deadlines();
    }
    {
        Pair pair;
        auto writer = pair.writer();
        expect(fcntl(writer.get(), F_SETPIPE_SZ, 4096) >= 4096, "Cannot set FIFO capacity");
        Bytes fill(512, 'x');
        std::size_t filled = 0;
        ssize_t count;
        while ((count = write(writer.get(), fill.data(), fill.size())) > 0) {
            filled += static_cast<std::size_t>(count);
        }
        Fd reader(open((pair.directory.path().toStdString() + "/up").c_str(), O_RDONLY | O_NONBLOCK));
        expect(reader.get() >= 0, "Cannot open drain reader");
        auto drain = std::async(std::launch::async, [&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            std::size_t drained = 0;
            auto deadline = Clock::now() + std::chrono::seconds(3);
            while (drained < filled && Clock::now() < deadline) {
                char bytes[512];
                ssize_t got = read(reader.get(), bytes, std::min(sizeof(bytes), filled - drained));
                if (got > 0) {
                    drained += static_cast<std::size_t>(got);
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
            return drained;
        });
        Message message;
        message.text = "after drain";
        auto packet = encode(message);
        pair.client->send(packet, pair.stop);
        expect(drain.get() == filled, "FIFO was not drained");
        Bytes got;
        expect(pair.host->receive(got, pair.stop, 100) == ReadResult::Data && got == packet,
               "Write did not recover after drain");
    }
    {
        Pair pair;
        auto writer = pair.writer();
        raw_write(writer.get(), Bytes{1, 0});
        Bytes packet;
        auto start = Clock::now();
        auto error = failure([&] {
            while (Clock::now() - start < std::chrono::seconds(7)) {
                pair.host->receive(packet, pair.stop, 100);
            }
        });
        expect(error == "Read timeout" && Clock::now() - start >= std::chrono::milliseconds(4900),
               "Partial packet deadline failed");
    }
    {
        Pair pair;
        auto writer = pair.writer();
        expect(fcntl(writer.get(), F_SETPIPE_SZ, 4096) >= 4096, "Cannot set FIFO capacity");
        Bytes fill(512, 'x');
        while (write(writer.get(), fill.data(), fill.size()) > 0) {}
        Message message;
        message.text = "blocked";
        auto start = Clock::now();
        auto error = failure([&] { pair.client->send(encode(message), pair.stop); });
        expect(error == "Write timeout" && Clock::now() - start >= std::chrono::milliseconds(4900) &&
                   Clock::now() - start < std::chrono::seconds(7), "Full FIFO deadline failed");
        pair.stop.store(true);
        expect(failure([&] { pair.client->send(encode(message), pair.stop); }) == "Cancelled",
               "Write was not cancelled");
    }
    return "Неполный и заполненный FIFO завершаются по тайм-ауту";
}

std::string resource_failures() {
    if (type != "mq") {
        QTemporaryDir directory("/tmp/os-chat-resource-XXXXXX");
        fs::path root = directory.path().toStdString();
        auto name = type == "fifo" ? "down" : "socket";
        write_file(root / name, "keep");
        auto conn = make_connection();
        failure([&] { conn->open(root.string(), true); });
        expect(!fs::exists(root / "up") && read_file(root / name) == "keep",
               "FIFO rollback removed foreign file");
    }
    else {
        QTemporaryDir directory("/tmp/os-chat-resource-XXXXXX");
        auto root = directory.path().toStdString();
        Queue occupied(queue_name(root, "down"), true, 1);
        auto conn = make_connection();
        failure([&] { conn->open(root, true); });
        expect(queue_missing(queue_name(root, "up")) && !queue_missing(queue_name(root, "down")) &&
                   occupied.attributes().mq_maxmsg == 1,
               "Queue rollback changed foreign resource");
    }
    {
        NamedSemaphore occupied;
        auto name = Session::semaphore_name(getpid(), type, "accepted");
        occupied.open(name, true, 7);
        failure([&] { Session session(make_connection(), getpid(), true); });
        expect(!fs::exists(Session::path(getpid(), type)), "Failed constructor left directory");
        sem_t* sem = sem_open(name.c_str(), 0);
        int value = 0;
        expect(sem != SEM_FAILED, "Foreign semaphore removed");
        expect(sem_getvalue(sem, &value) == 0 && value == 7, "Foreign semaphore changed");
        sem_close(sem);
    }
    check_resources_gone(getpid());
    {
        Session session(make_connection(), getpid(), true);
        failure([&] { Session duplicate(make_connection(), getpid(), true); });
        expect(fs::exists(Session::path(getpid(), type) + "/ready"), "Existing session damaged");
    }
    check_resources_gone(getpid());
    return "Откат и чужие ресурсы проверены";
}

std::string application_sessions(const fs::path& bin) {
    auto host_path = bin / ("host_" + type);
    auto client_path = bin / ("client_" + type);
    sigset_t signal_set;
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGUSR1);
    timespec delay{};
    while (sigtimedwait(&signal_set, nullptr, &delay) >= 0) {}
    auto invalid = run(QString::fromStdString(client_path.string()), {QString::number(getpid())}, bin);
    expect(invalid.code == 1 && !invalid.error.isEmpty(), "Non-host PID accepted");
    sigset_t pending;
    expect(sigpending(&pending) == 0 && !sigismember(&pending, SIGUSR1), "Non-host received SIGUSR1");
    invalid = run(QString::fromStdString(client_path.string()), {"2147483647"}, bin);
    expect(invalid.code == 1, "Missing PID accepted");
    {
        Session session(make_connection(), getpid(), true);
        auto ready = Session::path(getpid(), type) + "/ready";
        expect(unlink(ready.c_str()) == 0 && mkfifo(ready.c_str(), 0600) == 0, "Cannot prepare invalid marker");
        invalid = run(QString::fromStdString(client_path.string()), {QString::number(getpid())}, bin);
        expect(invalid.code == 1 && invalid.error.contains("Invalid session file"),
               "FIFO marker was accepted");
    }
    check_resources_gone(getpid());
    Child host(host_path, {});
    auto pid = host.pid();
    auto path = Session::path(pid, type);
    auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!fs::exists(path + "/ready") && Clock::now() < deadline) {
        QTest::qWait(20);
    }
    expect(fs::exists(path + "/ready"), "Host did not become ready");
    Child client(client_path, {QString::number(pid)});
    deadline = Clock::now() + std::chrono::seconds(5);
    while (!fs::exists(path + "/reply") && Clock::now() < deadline) {
        QTest::qWait(20);
    }
    expect(fs::exists(path + "/reply"), "Applications did not connect");
    host.process.terminate();
    success(host.finish());
    success(client.finish());
    check_resources_gone(pid);
    return "Приложения подключаются и завершаются";
}

Message sample(int sender, int recipient, std::uint64_t number, std::uint64_t time,
               const std::string& text) {
    Message message;
    message.kind = recipient == -1 ? MessageKind::Public : MessageKind::Private;
    message.sender = sender;
    message.recipient = recipient;
    message.number = number;
    message.sent_at = time;
    message.text = text;
    return message;
}

std::string chat_rules() {
    Chat host(0);
    Chat client(1);
    std::uint64_t number = 0;
    for (int sender : {0, 1}) {
        for (int recipient : {-1, 0, 1}) {
            ++number;
            auto message = sample(sender, recipient, number, number, "Привет, <b>чат</b>! 😀");
            auto host_size = host.history().size();
            auto client_size = client.history().size();
            expect(host.accept(message) && client.accept(message), "Valid message rejected");
            expect(host.history().size() == host_size + (recipient == -1 || sender == 0 || recipient == 0),
                   "Wrong host visibility");
            expect(client.history().size() == client_size + (recipient == -1 || sender == 1 || recipient == 1),
                   "Wrong client visibility");
        }
    }
    auto message = sample(0, -1, 100, 100, "");
    for (const auto& text : {std::string(), std::string("  \t\n"), std::string(1025, 'x')}) {
        message.text = text;
        failure([&] { host.accept(message); });
    }
    message.text = std::string(1020, 'a') + "😀";
    expect(host.accept(message), "Boundary UTF-8 rejected");
    message = sample(1, 9, 200, 200, "unknown");
    failure([&] { host.accept(message); });
    return "Общие и личные сообщения проверены";
}

bool ordered(const History& history) {
    return std::is_sorted(history.begin(), history.end(), [](const Message& a, const Message& b) {
        return std::tie(a.sent_at, a.sender, a.number) < std::tie(b.sent_at, b.sender, b.number);
    });
}

std::string chat_order() {
    Chat host(0);
    Chat client(1);
    History messages{sample(1, -1, 1, 30, "late"), sample(0, -1, 2, 20, "equal 2"),
                     sample(0, -1, 1, 20, "equal 1"), sample(1, -1, 2, 10, "early")};
    for (const auto& message : messages) {
        expect(host.accept(message) && client.accept(message), "Message rejected");
    }
    expect(ordered(host.history()) && host.history() == client.history() &&
               host.history().front().text == "early" && host.history()[1].text == "equal 1",
           "History order differs");
    expect(!host.accept(messages.front()) && host.history().size() == 4, "Duplicate added");
    return "Порядок одинаковый, дубликатов нет";
}

std::string chat_activity() {
    Deadline now{};
    Chat host(0, [&] { return now; });
    auto message = sample(0, -1, 1, 1, "host");
    now += std::chrono::seconds(60);
    expect(!host.idle(), "Idle deadline fired early");
    now += std::chrono::nanoseconds(1);
    expect(host.idle(), "Idle deadline missing");
    host.accept(message);
    expect(host.idle(), "Host activity reset client deadline");
    message.kind = MessageKind::System;
    message.sender = 1;
    expect(!host.accept(message) && host.idle(), "System packet reset deadline");
    now = {};
    Chat reset(0, [&] { return now; });
    now += std::chrono::seconds(59);
    message = sample(1, 1, 1, 1, "self");
    reset.accept(message);
    now += std::chrono::seconds(60);
    expect(!reset.idle(), "User message did not reset deadline");
    expect(!reset.accept(message), "Duplicate accepted");
    now += std::chrono::nanoseconds(1);
    expect(reset.idle(), "Duplicate reset deadline");
    Chat client(1, [&] { return now; });
    now += std::chrono::seconds(100);
    expect(!client.idle(), "Client applies host deadline");
    return "Минутный срок и сброс проверены";
}

std::string chat_widgets() {
    ChatWindow window("Host", QString::fromStdString(type), 123);
    auto* input = window.findChild<QLineEdit*>("text");
    auto* send = window.findChild<QPushButton*>("send");
    auto* recipient = window.findChild<QComboBox*>("recipient");
    auto* history = window.findChild<QListWidget*>("history");
    QSignalSpy requested(&window, &ChatWindow::send_requested);
    expect(requested.isValid(), "Cannot observe send signal");
    window.set_connected(true);
    expect(send->isEnabled() && input->isEnabled() && recipient->count() == 3,
           "Chat controls disabled");
    input->setText("  ");
    send->click();
    expect(requested.count() == 0, "Blank message emitted");
    input->setText(QString(1025, 'a'));
    send->click();
    expect(requested.count() == 0, "Long message emitted");
    recipient->setCurrentIndex(recipient->findData(1));
    input->setText("Привет 😀");
    QTest::keyClick(input, Qt::Key_Return);
    expect(requested.count() == 1 && requested.front()[0].toInt() == 1 &&
               requested.front()[1].toString() == "Привет 😀", "Wrong send signal");
    window.clear_input();
    expect(input->text().isEmpty(), "Input not cleared");
    window.set_history({sample(0, -1, 1, 1, "<b>текст</b>\n😀")});
    expect(history->count() == 1 && history->item(0)->text().contains("<b>текст</b>"),
           "HTML was not kept as text");
    window.set_connected(false);
    expect(!send->isEnabled() && !input->isEnabled(), "Disconnected input enabled");
    return "Ввод, адресат и обычный текст работают";
}

void bind_window(ChatWindow& window, Worker& worker) {
    QObject::connect(&worker, &Worker::status_changed, &window, &ChatWindow::set_status,
                     Qt::QueuedConnection);
    QObject::connect(&worker, &Worker::connected_changed, &window, &ChatWindow::set_connected,
                     Qt::QueuedConnection);
    QObject::connect(&worker, &Worker::history_changed, &window, &ChatWindow::set_history,
                     Qt::QueuedConnection);
    QObject::connect(&window, &ChatWindow::send_requested, &window, [&](int recipient, const QString& text) {
        if (worker.enqueue(recipient, text)) {
            window.clear_input();
        }
    });
}

void type_message(ChatWindow& window, int recipient, const QString& text) {
    auto* target = window.findChild<QComboBox*>("recipient");
    target->setCurrentIndex(target->findData(recipient));
    window.findChild<QLineEdit*>("text")->setText(text);
    window.findChild<QPushButton*>("send")->click();
}

int chat_client_main(QApplication& app, pid_t pid, const std::string& mode) {
    ChatWindow window("Client", QString::fromStdString(type), pid);
    Worker worker(make_connection(), pid, false);
    bind_window(window, worker);
    History history;
    bool complete = false;
    int ticks = 0;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &app, [&] { ++ticks; });
    timer.start(20);
    QObject::connect(&worker, &Worker::history_changed, &window, [&](const History& messages) {
        history = messages;
        if (mode == "exchange" && history.size() == 46) {
            bool valid = ordered(history);
            bool self = false;
            for (const auto& message : history) {
                valid = valid && message.text != "h self";
                self = self || message.text == "c self";
            }
            auto* list = window.findChild<QListWidget*>("history");
            complete = valid && self && list->count() == 46;
            QTimer::singleShot(100, &app, [&] { app.exit(complete ? 0 : 2); });
        }
    }, Qt::QueuedConnection);
    QObject::connect(&worker, &Worker::connected_changed, &window, [&](bool connected) {
        if (!connected) {
            return;
        }
        if (mode == "exchange") {
            type_message(window, -1, "c public");
            type_message(window, 0, "c private");
            type_message(window, 1, "c self");
            for (int i = 0; i < 20; ++i) {
                type_message(window, -1, QString("c burst %1").arg(i));
            }
            type_message(window, -1, "<b>Привет 😀</b>");
        } else {
            type_message(window, -1, "unconfirmed");
            if (mode == "cancel") {
                QTimer::singleShot(200, &window, &QWidget::close);
            }
        }
    }, Qt::QueuedConnection);
    QObject::connect(&worker, &Worker::done, &app, [&](int code) {
        app.exit(mode == "exchange" && !complete ? 2 : code);
    });
    QObject::connect(&app, &QApplication::aboutToQuit, &worker, &Worker::stop);
    if (mode == "cancel-connect") {
        QTimer::singleShot(200, &window, &QWidget::close);
    }
    QTimer::singleShot(20000, &app, [&] { app.exit(3); });
    window.show();
    worker.start();
    int code = app.exec();
    worker.stop();
    worker.wait();
    if (mode == "timeout" && (!history.empty() || ticks < 10)) {
        return 2;
    }
    return code;
}

std::string worker_chat(const fs::path& self) {
    ChatWindow window("Host", QString::fromStdString(type), getpid());
    Worker worker(make_connection(), getpid(), true);
    bind_window(window, worker);
    History history;
    QObject::connect(&worker, &Worker::history_changed, &window,
                     [&](const History& messages) { history = messages; }, Qt::QueuedConnection);
    QObject::connect(&worker, &Worker::connected_changed, &window, [&](bool connected) {
        if (!connected) {
            return;
        }
        type_message(window, -1, "h public");
        type_message(window, 1, "h private");
        type_message(window, 0, "h self");
        for (int i = 0; i < 20; ++i) {
            type_message(window, -1, QString("h burst %1").arg(i));
        }
    }, Qt::QueuedConnection);
    window.show();
    worker.start();
    auto ready = Session::path(getpid(), type) + "/ready";
    auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!fs::exists(ready) && Clock::now() < deadline) {
        QTest::qWait(20);
    }
    expect(fs::exists(ready), "Worker not ready");
    Child client(self, {"--chat-client", QString::number(getpid()), "exchange"});
    deadline = Clock::now() + std::chrono::seconds(15);
    while (client.process.state() != QProcess::NotRunning && Clock::now() < deadline) {
        QTest::qWait(20);
    }
    success(client.finish());
    expect(history.size() == 46 && ordered(history), "Wrong host history: " + std::to_string(history.size()));
    bool own = false;
    for (const auto& message : history) {
        expect(message.text != "c self", "Client private message leaked");
        own = own || message.text == "h self";
    }
    expect(own, "Host private-to-self missing");
    worker.stop();
    expect(worker.wait(1500), "Worker did not stop");
    window.close();
    check_resources_gone(getpid());
    return "Оба окна получили по 46 сообщений";
}

std::string worker_waits(const fs::path& self) {
    std::atomic_bool stop{false};
    for (const auto* mode : {"timeout", "cancel", "cancel-connect"}) {
        Session session(make_connection(), getpid(), true);
        Child client(self, {"--chat-client", QString::number(getpid()), mode});
        auto start = Clock::now();
        if (std::string(mode) != "cancel-connect") {
            accept_peer(session, stop);
            Message message;
            expect(receive_packet(session, message, stop) == ReadResult::Data &&
                       message.text == "unconfirmed", "Missing outgoing message");
        }
        auto result = client.finish();
        if (std::string(mode) == "timeout") {
            expect(result.code == 1 && result.error.contains("Delivery timeout") &&
                       Clock::now() - start >= std::chrono::milliseconds(4900),
                   "Unconfirmed message did not time out");
        } else {
            success(result);
            expect(Clock::now() - start < std::chrono::seconds(2), "Window close was blocked");
        }
    }
    check_resources_gone(getpid());
    for (const auto* mode : {"bye", "bye-burst"}) {
        std::atomic_bool readable{false};
        ChatWindow window("Host", QString::fromStdString(type), getpid());
        Worker worker(std::make_unique<DelayedConn>(&readable), getpid(), true);
        bind_window(window, worker);
        History history;
        QObject::connect(&worker, &Worker::history_changed, &window,
                         [&](const History& messages) { history = messages; }, Qt::QueuedConnection);
        QSignalSpy done(&worker, &Worker::done);
        worker.start();
        auto ready = Session::path(getpid(), type) + "/ready";
        auto deadline = Clock::now() + std::chrono::seconds(5);
        while (!fs::exists(ready) && Clock::now() < deadline) { QTest::qWait(20); }
        expect(fs::exists(ready), "Host worker not ready");
        Child peer(self, {"--peer", QString::number(getpid()), mode});
        success(peer.finish());
        readable.store(true);
        deadline = Clock::now() + std::chrono::seconds(5);
        while (done.isEmpty() && Clock::now() < deadline) { QTest::qWait(20); }
        QTest::qWait(20);
        expect(done.count() == 1 && done.front()[0].toInt() == 0,
               "Last message caused host error");
        auto count = std::string(mode) == "bye" ? 1U : 3U;
        expect(history.size() == count &&
                   window.findChild<QLabel*>("status")->text() == "Disconnected",
               "Last messages or disconnect status lost");
        for (unsigned i = 0; i < count; ++i) {
            auto text = count == 1 ? "last" : "last " + std::to_string(i + 1);
            expect(history[i].text == text, "Last message order changed");
        }
        worker.stop();
        expect(worker.wait(1500), "Host worker did not stop");
    }
    check_resources_gone(getpid());
    return "Окно отзывчиво, ожидания отменяются";
}

std::string idle_client(const fs::path& self, const fs::path& bin) {
    Child host(bin / ("host_" + type), {});
    auto pid = host.pid();
    auto path = Session::path(pid, type);
    auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!fs::exists(path + "/ready") && Clock::now() < deadline) {
        QTest::qWait(20);
    }
    expect(fs::exists(path + "/ready"), "Host not ready for idle test");
    Child client(self, {"--peer", QString::number(pid), "silent"});
    deadline = Clock::now() + std::chrono::seconds(5);
    while (!fs::exists(path + "/reply") && Clock::now() < deadline) {
        QTest::qWait(20);
    }
    expect(fs::exists(path + "/reply"), "Idle client did not connect");
    auto start = Clock::now();
    while (Clock::now() - start < std::chrono::seconds(59)) {
        QTest::qWait(50);
        expect(client.process.state() != QProcess::NotRunning, "Client killed before one minute");
    }
    expect(client.process.waitForFinished(8000), "Idle client survived deadline");
    expect(client.process.exitStatus() == QProcess::CrashExit && client.process.exitCode() == SIGKILL &&
               Clock::now() - start >= std::chrono::milliseconds(59500) &&
               Clock::now() - start < std::chrono::seconds(66), "Wrong inactivity termination");
    expect(host.process.state() != QProcess::NotRunning, "Host killed itself");
    host.process.terminate();
    success(host.finish());
    check_resources_gone(pid);
    return "Молчащий клиент завершён SIGKILL после минуты";
}

void closed_socket() {
    struct sigaction action{};
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    expect(sigaction(SIGPIPE, &action, nullptr) == 0, "Cannot restore SIGPIPE");
    Pair pair(true);
    pair.close_client();
    Message message;
    message.text = "closed";
    auto error = failure([&] { pair.host->send(encode(message), pair.stop); });
    expect(error == "Connection closed", "Closed socket write accepted");
}

std::string socket_errors(const fs::path& self) {
    {
        QTemporaryDir directory("/tmp/os-chat-resource-XXXXXX");
        fs::path root = directory.path().toStdString();
        fs::path long_path = root / std::string(110, 'a');
        fs::create_directory(long_path);
        for (bool create : {true, false}) {
            auto conn = make_connection();
            expect(failure([&] { conn->open(long_path.string(), create); }) == "Socket path too long",
                   "Long socket path accepted");
            expect(fs::is_empty(long_path), "Long path left files");
        }
        auto conn = make_connection();
        conn->open(root.string(), true);
        expect(unlink((root / "socket").c_str()) == 0, "Cannot replace test socket");
        write_file(root / "socket", "keep");
        conn.reset();
        expect(read_file(root / "socket") == "keep", "Foreign replacement removed");
        conn = make_connection();
        expect(failure([&] { conn->open(root.string(), false); }) == "Invalid socket",
               "Regular file accepted as socket");
    }
    {
        QTemporaryDir directory("/tmp/os-chat-resource-XXXXXX");
        auto path = directory.path().toStdString() + "/socket";
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
        Fd fd(socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
        expect(fd.get() >= 0 && bind(fd.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 &&
                   chmod(path.c_str(), 0600) == 0, "Cannot prepare refused socket");
        auto conn = make_connection();
        auto start = Clock::now();
        expect(failure([&] { conn->open(directory.path().toStdString(), false); })
                   .find("Cannot connect socket") != std::string::npos &&
                   Clock::now() - start < std::chrono::seconds(2), "Refused connection blocked");
        expect(fs::exists(path), "Client removed server socket");
    }
    {
        std::atomic_bool stop{false};
        std::unique_ptr<Child> peer;
        {
            Session session(make_connection(), getpid(), true);
            auto stranger = connect_socket(Session::path(getpid(), type));
            peer = std::make_unique<Child>(self, QStringList{"--peer", QString::number(getpid()), "hold"});
            expect(failure([&] { accept_peer(session, stop); }) == "Invalid client",
                   "Socket peer did not match SIGUSR1 sender");
        }
        expect(peer->finish().code == 1, "Wrong peer connection succeeded");
        check_resources_gone(getpid());
    }
    success(run(QString::fromStdString(self.string()), {"--closed-socket"}, self.parent_path()));
    return "Длина пути, чужие файлы, отказ и SIGPIPE проверены";
}

void wait_ready(pid_t pid, const std::string& transport) {
    auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!fs::exists(Session::path(pid, transport) + "/ready") && Clock::now() < deadline) {
        QTest::qWait(20);
    }
    expect(fs::exists(Session::path(pid, transport) + "/ready"), "Host not ready");
}

void wait_peer(Child& peer) {
    QByteArray output;
    auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!output.contains("ready") && Clock::now() < deadline) {
        QTest::qWait(20);
        output += peer.process.readAllStandardOutput();
    }
    expect(output.contains("ready"), "Pair client did not receive confirmation");
}

class HostCleanup {
public:
    explicit HostCleanup(Child& host) : host(host), pid(host.pid()) {}
    ~HostCleanup() {
        if (host.process.state() != QProcess::NotRunning) {
            host.process.terminate();
            if (!host.process.waitForFinished(1500)) {
                host.process.kill();
                host.process.waitForFinished(5000);
            }
        }
        auto directory = Session::path(pid, "mq");
        for (const auto* suffix : {"up", "down"}) {
            mq_unlink(queue_name(directory, suffix).c_str());
        }
        for (const auto* suffix : {"slot", "accepted", "up", "down"}) {
            sem_unlink(Session::semaphore_name(pid, "mq", suffix).c_str());
        }
        for (const auto* name : {"ready", "request", "reply"}) {
            unlink((directory + "/" + name).c_str());
        }
        rmdir(directory.c_str());
    }

private:
    Child& host;
    pid_t pid;
};

std::string queue_failures(const fs::path& bin) {
    {
        QTemporaryDir directory("/tmp/os-chat-resource-XXXXXX");
        auto root = directory.path().toStdString();
        auto conn = make_connection();
        failure([&] { conn->open(root, false); });
        expect(queue_missing(queue_name(root, "up")) && queue_missing(queue_name(root, "down")),
               "Client created missing queues");
        Queue wrong(queue_name(root, "down"), true, 1, 128);
        expect(failure([&] { conn->open(root, false); }) == "Invalid queue",
               "Foreign queue attributes accepted");
        expect(!queue_missing(queue_name(root, "down")) && wrong.attributes().mq_msgsize == 128,
               "Client changed foreign queue");
    }
    for (bool stop_host : {false, true}) {
        Child host(bin / "host_mq", {});
        auto pid = host.pid();
        HostCleanup cleanup(host);
        wait_ready(pid, "mq");
        Child client(bin / "client_mq", {QString::number(pid)});
        auto directory = Session::path(pid, "mq");
        auto deadline = Clock::now() + std::chrono::seconds(5);
        while (!fs::exists(directory + "/reply") && Clock::now() < deadline) { QTest::qWait(20); }
        expect(fs::exists(directory + "/reply"), "Queue applications did not connect");
        auto start = Clock::now();
        if (stop_host) {
            host.process.kill();
            host.finish();
            success(client.finish());
            expect(Clock::now() - start < std::chrono::seconds(2), "Client waited for queue EOF");
        } else {
            client.process.kill();
            client.finish();
            while (fs::exists(directory) && Clock::now() - start < std::chrono::seconds(2)) {
                QTest::qWait(20);
            }
            expect(!fs::exists(directory) && host.process.state() != QProcess::NotRunning,
                   "Host missed queue client exit");
            host.process.terminate();
            success(host.finish());
            check_resources_gone(pid, "mq");
        }
    }
    return "Чужие очереди сохранены, потеря процесса обнаружена";
}

std::string isolation(const fs::path& self, const fs::path& bin) {
    if (type == "sock") {
        for (const auto& transport : {std::string("fifo"), std::string("sock"), std::string("mq")}) {
            Child host(bin / ("host_" + transport), {});
            auto pid = host.pid();
            wait_ready(pid, transport);
            for (const auto& other : {std::string("fifo"), std::string("sock"), std::string("mq")}) {
                if (other == transport) { continue; }
                auto result = run(QString::fromStdString((bin / ("client_" + other)).string()),
                                  {QString::number(pid)}, bin);
                expect(result.code == 1 && !result.error.isEmpty(), "Mismatched IPC connected");
                expect(host.process.state() != QProcess::NotRunning &&
                           fs::exists(Session::path(pid, transport) + "/ready"), "Wrong client damaged host");
            }
            host.process.terminate();
            success(host.finish());
            check_resources_gone(pid, transport);
        }
    }
    Child first(bin / ("host_" + type), {});
    Child second(bin / ("host_" + type), {});
    auto first_pid = first.pid();
    auto second_pid = second.pid();
    wait_ready(first_pid, type);
    wait_ready(second_pid, type);
    Child a(self, {"--peer", QString::number(first_pid), "pair-a"});
    Child b(self, {"--peer", QString::number(second_pid), "pair-b"});
    wait_peer(a);
    wait_peer(b);
    first.process.terminate();
    success(first.finish());
    auto result = a.finish();
    expect(result.code == 0 || result.error.contains("closed") || result.error.contains("write stream"),
           "First peer failed unexpectedly");
    expect(second.process.state() != QProcess::NotRunning, "Other host was stopped");
    success(b.finish());
    second.process.terminate();
    success(second.finish());
    check_resources_gone(first_pid);
    check_resources_gone(second_pid);
    Child again(bin / ("host_" + type), {});
    auto pid = again.pid();
    wait_ready(pid, type);
    Child peer(self, {"--peer", QString::number(pid), "pair-new"});
    success(peer.finish());
    again.process.terminate();
    success(again.finish());
    check_resources_gone(pid);
    return "Пары изолированы, повторный запуск работает";
}

int sanitizer_reports(const fs::path& directory) {
    bool failed = false;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.file_size() != 0) {
            std::cerr << entry.path().filename().string() << ":\n"
                      << read_file(entry.path()).toStdString() << '\n';
            failed = true;
        }
    }
    if (!failed) { std::cout << "Санитайзеры: ошибок нет\n"; }
    return failed ? 1 : 0;
}

}

int main(int argc, char* argv[]) {
    if (argc == 3 && std::string(argv[1]) == "--reports") {
        try { return sanitizer_reports(argv[2]); }
        catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    }
    Session::block_signals();
    if (argc == 2 && std::string(argv[1]) == "--closed-socket") {
        try {
            closed_socket();
            return 0;
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    if (argc == 4 && std::string(argv[1]) == "--peer") {
        return peer_main(read_pid(argv[2]), argv[3]);
    }
    QApplication app(argc, argv);
    qRegisterMetaType<History>();
    if (argc == 4 && std::string(argv[1]) == "--chat-client") {
        return chat_client_main(app, read_pid(argv[2]), argv[3]);
    }
    if (argc != 2) {
        std::cerr << "Usage: run_tests <project_dir>\n";
        return 1;
    }
    fs::path source = fs::absolute(argv[1]);
    fs::path bin = QCoreApplication::applicationDirPath().toStdString();
    fs::path self = QCoreApplication::applicationFilePath().toStdString();
    struct Test {
        const char* name;
        std::function<std::string()> run;
        bool common = false;
    };
    std::vector<Test> tests = {
        {"Чистая и повторная сборка", [&] { return build_commands(source); }, true},
        {"Автоматическое обнаружение исходников и флаги", [&] { return automatic_sources(source); }, true},
        {"Ошибка компиляции и очистка", [&] { return build_failure(source); }, true},
        {"Аргументы и справка", [&] { return arguments(bin, source); }},
        {"Окна и начальное состояние", [&] { return windows(app); }},
        {"Формат сообщений и UTF-8", [&] { return protocol(); }, true},
        {"Знакомство и обмен", [&] { return session_exchange(self); }},
        {"Второй клиент и спокойное ожидание", [&] { return busy_and_idle(self); }},
        {"Пакеты и отключение", [&] { return stream_packets(self); }},
        {"Семафоры и тайм-ауты", [&] { return semaphore_deadlines(self, bin); }},
        {"Тайм-ауты и отмена", [&] { return stream_deadlines(); }},
        {"Ошибки создания ресурсов", [&] { return resource_failures(); }},
        {"Запуск настоящих приложений", [&] { return application_sessions(bin); }},
        {"Адресация и проверка текста", [&] { return chat_rules(); }, true},
        {"Порядок истории и дубликаты", [&] { return chat_order(); }, true},
        {"Логика неактивности", [&] { return chat_activity(); }, true},
        {"Ввод и отображение Qt", [&] { return chat_widgets(); }, true},
        {"Чат между двумя окнами", [&] { return worker_chat(self); }},
        {"Подтверждение и закрытие окна", [&] { return worker_waits(self); }},
        {"Настоящая минута неактивности", [&] { return idle_client(self, bin); }}
    };
    if (type != "fifo") {
        tests.erase(std::remove_if(tests.begin(), tests.end(), [](const Test& test) {
            return test.common;
        }), tests.end());
    }
    if (type == "sock") {
        tests.push_back({"Ошибки и закрытие сокета", [&] { return socket_errors(self); }});
    }
    if (type == "mq") {
        tests.push_back({"Ошибки очередей и потеря процесса", [&] { return queue_failures(bin); }});
    }
    if (type == "sock" || type == "mq") {
        tests.push_back({"Изоляция независимых пар", [&] { return isolation(self, bin); }});
    }
    std::cout << "IPC: " << type << "\n\n";
    int passed = 0;
    int total = 0;
    for (const auto& test : tests) {
        std::cout << ++total << ". " << test.name << std::endl;
        try {
            const auto result = test.run();
            std::cout << "Получено: " << result << "\nРезультат: пройден\n\n";
            ++passed;
        } catch (const std::exception& error) {
            std::cout << "Получено: " << error.what() << "\nРезультат: ошибка\n\n";
        }
    }
    std::cout << "Выполнено: " << total << ", пройдено: " << passed
              << ", ошибок: " << total - passed << '\n';
    return passed == total ? 0 : 1;
}
