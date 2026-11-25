#include "wolf/client.hpp"
#include "wolf/conn.hpp"
#include "wolf/host_window.hpp"
#include "wolf/shared_state.hpp"
#include "wolf/transport.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QLabel>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QThread>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
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

bool HasLabel(QWidget& window, const QString& text) {
    for (QLabel* label : window.findChildren<QLabel*>()) {
        if (label->text().contains(text)) {
            return true;
        }
    }
    return false;
}

void StopChild(pid_t child) {
    int status = 0;
    const pid_t state = waitpid(child, &status, WNOHANG);
    if (state == child || (state == -1 && errno == ECHILD)) {
        return;
    }
    kill(child, SIGTERM);
    while (waitpid(child, &status, 0) == -1 && errno == EINTR) {
    }
}

}

int main(int argc, char* argv[]) {
    wolf::SharedStateArea area;
    wolf::PrepareTransport(area.Get());

    const pid_t child = fork();
    if (child < 0) {
        std::cerr << "fork не удался\n";
        return EXIT_FAILURE;
    }
    if (child == 0) {
        const int code = wolf::RunClient(area.Get());
        _exit(code);
    }

    int result = EXIT_FAILURE;
    try {
        std::unique_ptr<wolf::Conn> connection =
            wolf::OpenTransport(wolf::Role::Host, area.Get());

        QApplication application(argc, argv);
        {
            wolf::HostWindow window(connection.get(), child);
            window.show();

            QSpinBox* input = window.findChild<QSpinBox*>();
            QPlainTextEdit* log = window.findChild<QPlainTextEdit*>();
            Require(input != nullptr && log != nullptr, "элементы окна не найдены");

            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(25);
            bool finished = false;
            while (std::chrono::steady_clock::now() < deadline) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                QThread::msleep(10);

                if (HasLabel(window, "Работа остановлена")) {
                    throw std::runtime_error("хост сообщил об ошибке: " + log->toPlainText().toStdString());
                }
                if (input->isEnabled()) {
                    input->setValue(100);
                    QMetaObject::invokeMethod(input, "editingFinished");
                }
                if (HasLabel(window, "Игра завершена")) {
                    finished = true;
                    break;
                }
            }

            Require(finished, "игра не завершилась за отведённое время");
            Require(log->toPlainText().contains("волк 100"), "в журнале нет хода волка");
            Require(log->toPlainText().contains("козлёнок"), "в журнале нет хода козлёнка");
        }

        result = EXIT_SUCCESS;
        std::cout << "тест обмена пройден\n";
    } catch (const std::exception& exception) {
        std::cerr << "тест не пройден: " << exception.what() << '\n';
    }

    StopChild(child);
    return result;
}
