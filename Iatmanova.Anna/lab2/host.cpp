#include "common.hpp"
#include CONN_HEADER

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/select.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {

bool WaitForPacket(Conn& conn, ChatPacket& packet, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (conn.Read(&packet, sizeof(packet))) {
            return true;
        }
        if (conn.Failed()) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

int RunSelfTest() {
    SharedSync* sync = CreateSharedSync();
    if (sync == nullptr) {
        return 2;
    }

    const pid_t host_pid = getpid();
    const pid_t child = fork();
    if (child == -1) {
        std::perror("fork");
        DestroySharedSync(sync);
        return 2;
    }

    if (child == 0) {
        Conn conn(Role::Client, host_pid, sync);
        if (conn.Failed()) {
            _exit(3);
        }
        ChatPacket incoming{};
        if (!WaitForPacket(conn, incoming, 5000) || PacketText(incoming) != "ping") {
            _exit(4);
        }
        const ChatPacket pong = MakePacket(MessageKind::Private, 2, "client", "pong");
        if (!conn.Write(&pong, sizeof(pong))) {
            _exit(5);
        }
        _exit(0);
    }

    Conn conn(Role::Host, host_pid, sync);
    if (conn.Failed()) {
        kill(child, SIGKILL);
        waitpid(child, nullptr, 0);
        DestroySharedSync(sync);
        return 3;
    }

    const ChatPacket ping = MakePacket(MessageKind::Private, 1, "host", "ping");
    if (!conn.Write(&ping, sizeof(ping))) {
        kill(child, SIGKILL);
        waitpid(child, nullptr, 0);
        DestroySharedSync(sync);
        return 4;
    }

    ChatPacket reply{};
    const bool received = WaitForPacket(conn, reply, 5000);
    int status = 0;
    waitpid(child, &status, 0);
    DestroySharedSync(sync);

    if (!received || PacketText(reply) != "pong" || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::cerr << "[SELFTEST FAIL] " << CONN_TYPE_CODE << '\n';
        return 5;
    }

    std::cout << "[SELFTEST PASS] " << CONN_TYPE_CODE << '\n';
    return 0;
}

void PrintClientMessage(const ChatPacket& packet) {
    const auto kind = static_cast<MessageKind>(packet.kind);
    std::cout << "[" << KindName(kind) << "] " << PacketSender(packet) << ": "
              << PacketText(packet) << std::endl;
}

void ClientLoop(pid_t host_pid, SharedSync* sync) {
    Conn conn(Role::Client, host_pid, sync);
    if (conn.Failed()) {
        std::cerr << "Client connection error: " << conn.Error() << '\n';
        _exit(2);
    }

    std::uint64_t sequence = 1;
    const ChatPacket hello = MakePacket(MessageKind::System, sequence++, "client", "client connected");
    conn.Write(&hello, sizeof(hello));

    std::cout << "Client is running in this terminal. Commands:\n"
              << "  text              - public message\n"
              << "  /all text         - public message\n"
              << "  /private text     - private message to host\n"
              << "  /quit             - exit client\n";

    bool stdin_open = true;
    while (!conn.Failed()) {
        ChatPacket packet{};
        while (conn.Read(&packet, sizeof(packet))) {
            PrintClientMessage(packet);
        }
        if (conn.Failed()) {
            break;
        }

        if (stdin_open) {
            fd_set set;
            FD_ZERO(&set);
            FD_SET(STDIN_FILENO, &set);
            timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = 100000;
            const int rc = select(STDIN_FILENO + 1, &set, nullptr, nullptr, &tv);
            if (rc > 0 && FD_ISSET(STDIN_FILENO, &set)) {
                std::string line;
                if (!std::getline(std::cin, line)) {
                    stdin_open = false;
                } else if (line == "/quit") {
                    break;
                } else if (!line.empty()) {
                    MessageKind kind = MessageKind::Public;
                    std::string text = line;
                    const std::string private_prefix = "/private ";
                    const std::string all_prefix = "/all ";
                    if (line.rfind(private_prefix, 0) == 0) {
                        kind = MessageKind::Private;
                        text = line.substr(private_prefix.size());
                    } else if (line.rfind(all_prefix, 0) == 0) {
                        text = line.substr(all_prefix.size());
                    }
                    const ChatPacket outgoing = MakePacket(kind, sequence++, "client", text);
                    if (!conn.Write(&outgoing, sizeof(outgoing))) {
                        break;
                    }
                    PrintClientMessage(outgoing);
                }
            } else if (rc == -1 && errno != EINTR) {
                std::perror("select");
                break;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    _exit(0);
}

QString FormatPacket(const ChatPacket& packet) {
    const auto kind = static_cast<MessageKind>(packet.kind);
    return QString("[%1] %2: %3")
        .arg(QString::fromStdString(KindName(kind)))
        .arg(QString::fromStdString(PacketSender(packet)))
        .arg(QString::fromStdString(PacketText(packet)));
}

int RunGui(int argc, char** argv) {
    SharedSync* sync = CreateSharedSync();
    if (sync == nullptr) {
        return 2;
    }

    const pid_t host_pid = getpid();
    const pid_t child = fork();
    if (child == -1) {
        std::perror("fork");
        DestroySharedSync(sync);
        return 2;
    }
    if (child == 0) {
        ClientLoop(host_pid, sync);
    }

    Conn conn(Role::Host, host_pid, sync);
    if (conn.Failed()) {
        std::cerr << "Host connection error: " << conn.Error() << '\n';
        kill(child, SIGKILL);
        waitpid(child, nullptr, 0);
        DestroySharedSync(sync);
        return 3;
    }

    QApplication app(argc, argv);
    QWidget window;
    window.setWindowTitle(QString("OS Lab 2 - variant 13 - %1").arg(CONN_TYPE_CODE));
    window.resize(720, 520);

    auto* layout = new QVBoxLayout(&window);
    auto* status = new QLabel(QString("Host PID: %1 | Client PID: %2 | IPC: %3")
                                  .arg(host_pid)
                                  .arg(child)
                                  .arg(CONN_TYPE_CODE));
    auto* log = new QTextEdit;
    log->setReadOnly(true);
    auto* row = new QHBoxLayout;
    auto* mode = new QComboBox;
    mode->addItem("Общее сообщение", static_cast<int>(MessageKind::Public));
    mode->addItem("Личное клиенту", static_cast<int>(MessageKind::Private));
    auto* input = new QLineEdit;
    input->setPlaceholderText("Сообщение от host...");
    auto* send = new QPushButton("Отправить");
    row->addWidget(mode);
    row->addWidget(input, 1);
    row->addWidget(send);
    layout->addWidget(status);
    layout->addWidget(log, 1);
    layout->addLayout(row);

    log->append(QString("[STATUS] host started; IPC=%1").arg(CONN_TYPE_CODE));
    log->append(QString("[STATUS] client process created; PID=%1").arg(child));

    auto last_client_activity = std::chrono::steady_clock::now();
    std::uint64_t sequence = 1;
    bool child_alive = true;

    auto sendFromHost = [&]() {
        const QString text = input->text().trimmed();
        if (text.isEmpty() || !child_alive) {
            return;
        }
        const auto kind = static_cast<MessageKind>(mode->currentData().toInt());
        const ChatPacket packet = MakePacket(kind,
                                             sequence++,
                                             "host",
                                             text.toStdString());
        if (conn.Write(&packet, sizeof(packet))) {
            log->append(FormatPacket(packet));
            input->clear();
        } else {
            log->append(QString("[ERROR] send failed: %1").arg(QString::fromStdString(conn.Error())));
        }
    };

    QObject::connect(send, &QPushButton::clicked, &window, sendFromHost);
    QObject::connect(input, &QLineEdit::returnPressed, &window, sendFromHost);

    QTimer poll_timer;
    QObject::connect(&poll_timer, &QTimer::timeout, &window, [&]() {
        ChatPacket packet{};
        while (conn.Read(&packet, sizeof(packet))) {
            last_client_activity = std::chrono::steady_clock::now();
            log->append(FormatPacket(packet));
        }
        if (conn.Failed()) {
            status->setText(QString("Connection error: %1").arg(QString::fromStdString(conn.Error())));
        }

        if (child_alive) {
            int child_status = 0;
            const pid_t rc = waitpid(child, &child_status, WNOHANG);
            if (rc == child) {
                child_alive = false;
                status->setText(QString("Client finished | IPC: %1").arg(CONN_TYPE_CODE));
                log->append("[STATUS] client finished");
            }
        }
    });
    poll_timer.start(100);

    QTimer idle_timer;
    QObject::connect(&idle_timer, &QTimer::timeout, &window, [&]() {
        if (!child_alive) {
            return;
        }
        const auto idle = std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::steady_clock::now() - last_client_activity)
                              .count();
        if (idle >= kClientIdleTimeoutSec) {
            log->append("[STATUS] client inactive for 60 seconds -> SIGKILL");
            if (kill(child, SIGKILL) == 0) {
                child_alive = false;
                waitpid(child, nullptr, 0);
                status->setText(QString("Client killed due to inactivity | IPC: %1").arg(CONN_TYPE_CODE));
            } else {
                log->append(QString("[ERROR] SIGKILL failed: %1")
                                .arg(QString::fromLocal8Bit(std::strerror(errno))));
            }
        }
    });
    idle_timer.start(1000);

    QObject::connect(&app, &QApplication::aboutToQuit, &window, [&]() {
        if (child_alive) {
            kill(child, SIGTERM);
            waitpid(child, nullptr, 0);
            child_alive = false;
        }
    });

    window.show();
    const int rc = app.exec();
    DestroySharedSync(sync);
    return rc;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--self-test") {
        return RunSelfTest();
    }
    return RunGui(argc, argv);
}
