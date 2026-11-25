#include "wolf/conn.hpp"
#include "wolf/host_window.hpp"
#include "wolf/protocol.hpp"
#include "wolf/shared_state.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QLabel>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QThread>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class FakeConn final : public wolf::Conn {
public:
    explicit FakeConn(wolf::SharedState* state) : Conn(wolf::Role::Host, state) {}

    bool Read(void* buffer, std::size_t size) override {
        const wolf::Message reply{wolf::GoatThrow, round_, 1, 0};
        std::memcpy(buffer, &reply, size);
        return true;
    }

    bool Write(const void* buffer, std::size_t size) override {
        wolf::Message message{};
        std::memcpy(&message, buffer, size);
        if (message.type == wolf::RoundStart) {
            round_ = message.round;
        }
        return true;
    }

private:
    int round_ = 0;
};

bool HasText(QWidget& window, const QString& text) {
    for (QLabel* label : window.findChildren<QLabel*>()) {
        if (label->text().contains(text)) {
            return true;
        }
    }
    return false;
}

}

int main(int argc, char* argv[]) {
    pid_t child = fork();
    if (child < 0) {
        std::cerr << "fork не удался\n";
        return EXIT_FAILURE;
    }
    if (child == 0) {
        for (;;) {
            pause();
        }
    }

    int result = EXIT_FAILURE;
    try {
        QApplication application(argc, argv);
        wolf::SharedStateArea area;
        FakeConn connection(area.Get());

        {
            wolf::HostWindow window(&connection, child);
            window.show();

            QSpinBox* input = window.findChild<QSpinBox*>();
            QPlainTextEdit* log = window.findChild<QPlainTextEdit*>();
            Require(input != nullptr && log != nullptr, "элементы окна не найдены");

            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            bool finished = false;
            while (std::chrono::steady_clock::now() < deadline) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                QThread::msleep(10);

                if (input->isEnabled()) {
                    input->setValue(100);
                    QMetaObject::invokeMethod(input, "editingFinished");
                }
                if (HasText(window, "Игра завершена")) {
                    finished = true;
                    break;
                }
            }

            Require(finished, "игра не завершилась за отведённое время");
            Require(log->toPlainText().contains("волк 100, козлёнок 1"),
                    "в журнале нет данных раунда");
        }

        result = EXIT_SUCCESS;
        std::cout << "тест окна пройден\n";
    } catch (const std::exception& exception) {
        std::cerr << "тест не пройден: " << exception.what() << '\n';
    }

    kill(child, SIGTERM);
    int status = 0;
    while (waitpid(child, &status, 0) == -1) {
        if (errno != EINTR) {
            break;
        }
    }
    return result;
}
