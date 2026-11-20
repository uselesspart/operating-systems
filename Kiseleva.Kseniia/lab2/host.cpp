
#include "connection.h"

#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

std::uint64_t Timestamp() {
    using namespace std::chrono;

    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()
        ).count()
    );
}

void CopyText(
    char* destination,
    std::size_t size,
    const std::string& source
) {
    std::snprintf(destination, size, "%s", source.c_str());
}

QString TimeString(std::uint64_t timestamp) {
    const auto seconds = timestamp / 1000;
    const auto milliseconds = timestamp % 1000;

    return QString("%1.%2")
        .arg(seconds)
        .arg(milliseconds, 3, 10, QChar('0'));
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    int host_fd = -1;
    int client_fd = -1;

    if (PrepareConnection(host_fd, client_fd) != 0) {
        std::cerr << "Не удалось подготовить IPC.\n";
        return 1;
    }

    auto* sync = static_cast<SharedSync*>(
        mmap(
            nullptr,
            sizeof(SharedSync),
            PROT_READ | PROT_WRITE,
            MAP_SHARED | MAP_ANONYMOUS,
            -1,
            0
        )
    );

    if (sync == MAP_FAILED) {
        std::cerr << "Ошибка mmap.\n";
        return 1;
    }

    if (sem_init(&sync->host_to_client, 1, 0) == -1 ||
        sem_init(&sync->client_to_host, 1, 0) == -1) {
        std::cerr << "Ошибка sem_init.\n";
        munmap(sync, sizeof(SharedSync));
        return 1;
    }

    const pid_t child = fork();

    if (child == -1) {
        std::cerr << "Ошибка fork.\n";
        return 1;
    }

    if (child == 0) {
        if (host_fd >= 0) {
            close(host_fd);
        }

        const int result = RunClient(getppid(), sync, client_fd);
        _exit(result);
    }

    if (client_fd >= 0) {
        close(client_fd);
    }

    auto connection = CreateConnection(
        ProcessRole::Host,
        getpid(),
        sync,
        host_fd
    );

    if (!connection) {
        kill(child, SIGKILL);
        waitpid(child, nullptr, 0);
        return 1;
    }

    QMainWindow window;
    window.setWindowTitle("Локальный чат — Host");
    window.resize(700, 500);

    auto* central = new QWidget(&window);
    auto* layout = new QVBoxLayout(central);

    auto* status = new QLabel("Клиент подключён", central);

    auto* messages = new QTextEdit(central);
    messages->setReadOnly(true);

    auto* type = new QComboBox(central);
    type->addItem("Публичное сообщение");
    type->addItem("Личное сообщение");

    auto* recipient = new QLineEdit("client", central);
    recipient->setPlaceholderText("Получатель");

    auto* input = new QLineEdit(central);
    input->setPlaceholderText("Введите сообщение");

    auto* send = new QPushButton("Отправить", central);

    auto* controls = new QHBoxLayout();
    controls->addWidget(type);
    controls->addWidget(recipient);
    controls->addWidget(input);
    controls->addWidget(send);

    layout->addWidget(status);
    layout->addWidget(messages);
    layout->addLayout(controls);

    window.setCentralWidget(central);

    bool client_alive = true;

    auto last_client_activity =
        std::chrono::steady_clock::now();

    auto last_heartbeat =
        std::chrono::steady_clock::now();

    QObject::connect(send, &QPushButton::clicked, [&]() {
        if (!client_alive) {
            return;
        }

        const QString text = input->text().trimmed();

        if (text.isEmpty()) {
            return;
        }

        ChatMessage message{};
        message.timestamp_ms = Timestamp();

        CopyText(
            message.sender,
            sizeof(message.sender),
            "host"
        );

        CopyText(
            message.text,
            sizeof(message.text),
            text.toStdString()
        );

        if (type->currentIndex() == 0) {
            message.type = MessageType::Public;

            CopyText(
                message.recipient,
                sizeof(message.recipient),
                "all"
            );
        } else {
            message.type = MessageType::Private;

            CopyText(
                message.recipient,
                sizeof(message.recipient),
                recipient->text().toStdString()
            );
        }

        if (connection->Write(message, 5000)) {
            messages->append(
                "[" + TimeString(message.timestamp_ms) +
                "] host: " + text
            );
        } else {
            status->setText("Ошибка отправки сообщения");
        }

        input->clear();
    });

    QObject::connect(
        input,
        &QLineEdit::returnPressed,
        send,
        &QPushButton::click
    );

    QTimer timer;

    QObject::connect(&timer, &QTimer::timeout, [&]() {
        if (!client_alive) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();

        if (now - last_heartbeat >= std::chrono::seconds(2)) {
            ChatMessage heartbeat{};

            heartbeat.type = MessageType::Heartbeat;
            heartbeat.timestamp_ms = Timestamp();

            CopyText(
                heartbeat.sender,
                sizeof(heartbeat.sender),
                "host"
            );

            if (!connection->Write(heartbeat, 1000)) {
                client_alive = false;
                status->setText("Соединение с клиентом потеряно");
                return;
            }

            last_heartbeat = now;
        }

        ChatMessage message{};

        while (connection->Read(message, 0)) {
            last_client_activity =
                std::chrono::steady_clock::now();

            if (message.type == MessageType::Exit) {
                client_alive = false;
                status->setText("Клиент отключился");
                break;
            }

            if (message.type == MessageType::Heartbeat) {
                continue;
            }

            messages->append(
                "[" + TimeString(message.timestamp_ms) +
                "] " + QString::fromUtf8(message.sender) +
                ": " + QString::fromUtf8(message.text)
            );

            if (message.type == MessageType::Public) {
                message.timestamp_ms = Timestamp();

                if (!connection->Write(message, 1000)) {
                    status->setText(
                        "Не удалось переслать сообщение"
                    );
                }
            }
        }

        // Контроль активности клиента.
        if (client_alive &&
            std::chrono::steady_clock::now() -
                last_client_activity > std::chrono::seconds(60)) {
            kill(child, SIGKILL);
            client_alive = false;

            status->setText(
                "Клиент отключён: тайм-аут 60 секунд"
            );
        }

        int child_status = 0;

        const pid_t result =
            waitpid(child, &child_status, WNOHANG);

        if (result == child) {
            client_alive = false;
            status->setText("Процесс клиента завершён");
        }
    });

    timer.start(50);

    QObject::connect(
        &app,
        &QApplication::aboutToQuit,
        [&]() {
            if (client_alive) {
                ChatMessage message{};

                message.type = MessageType::Exit;
                message.timestamp_ms = Timestamp();

                CopyText(
                    message.sender,
                    sizeof(message.sender),
                    "host"
                );

                connection->Write(message, 1000);

                const auto deadline =
                    std::chrono::steady_clock::now() +
                    std::chrono::seconds(2);

                int child_status = 0;
                pid_t result = 0;

                while (std::chrono::steady_clock::now() < deadline) {
                    result = waitpid(
                        child,
                        &child_status,
                        WNOHANG
                    );

                    if (result == child || result == -1) {
                        break;
                    }

                    usleep(50000);
                }

                // Если клиент не завершился самостоятельно, завершаем его принудительно.
                if (result == 0) {
                    kill(child, SIGKILL);
                    waitpid(child, nullptr, 0);
                }
            }
        }
    );

    window.show();

    const int result = app.exec();

    connection.reset();

    sem_destroy(&sync->host_to_client);
    sem_destroy(&sync->client_to_host);

    munmap(sync, sizeof(SharedSync));

    return result;
}
