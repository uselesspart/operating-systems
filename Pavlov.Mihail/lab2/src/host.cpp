#include "game.hpp"

#include <QApplication>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <utility>

namespace {

class HostWindow final : public QWidget {
public:
    explicit HostWindow(std::unique_ptr<wolf::GameSession> session)
        : session_(std::move(session))
    {
        setWindowTitle(QStringLiteral("Волк и семеро козлят"));
        resize(570, 360);
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(&status_);
        log_.setReadOnly(true);
        layout->addWidget(&log_);

        auto* controls = new QHBoxLayout;
        controls->addWidget(new QLabel(QStringLiteral("Число волка"), this));
        auto* validator = new QIntValidator(1, 100, &number_);
        auto locale = QLocale::c();
        locale.setNumberOptions(QLocale::RejectGroupSeparator);
        validator->setLocale(locale);
        number_.setValidator(validator);
        number_.setPlaceholderText(QStringLiteral("От 1 до 100"));
        controls->addWidget(&number_);
        controls->addWidget(&submit_);
        layout->addLayout(controls);

        inputTimer_.setSingleShot(true);
        inputTimer_.setTimerType(Qt::PreciseTimer);
        pollTimer_.setInterval(20);
        connect(&inputTimer_, &QTimer::timeout, this, [this] {
            startRound(wolf::rollWolfNumber());
        });
        connect(&pollTimer_, &QTimer::timeout, this, [this] { showResult(); });
        connect(&number_, &QLineEdit::textChanged, this, [this] {
            submit_.setEnabled(number_.hasAcceptableInput());
        });
        connect(&number_, &QLineEdit::returnPressed, this, [this] { submitNumber(); });
        connect(&submit_, &QPushButton::clicked, this, [this] { submitNumber(); });

        if (session_) {
            log_.appendPlainText(QStringLiteral("Козлёнок подключился."));
            waitForNumber();
        } else {
            closeGame(QStringLiteral("Не удалось подключить козлёнка."));
        }
    }

    void showStartupError(const QString& error)
    {
        log_.appendPlainText(error);
    }

private:
    void waitForNumber()
    {
        status_.setText(QStringLiteral("Введите число и нажмите Enter в течение 3 секунд."));
        number_.clear();
        number_.setEnabled(true);
        submit_.setEnabled(false);
        number_.setFocus();
        inputTimer_.start(3000);
    }

    void submitNumber()
    {
        if (inputTimer_.isActive() && number_.hasAcceptableInput()) {
            startRound(number_.text().toInt());
        }
    }

    void startRound(int number)
    {
        inputTimer_.stop();
        number_.setEnabled(false);
        submit_.setEnabled(false);
        status_.setText(QStringLiteral("Идёт раунд…"));
        try {
            // Ожидание семафора выполняется вне потока интерфейса.
            pending_ = std::async(std::launch::async, [session = session_.get(), number] {
                return session->play(number);
            });
            pollTimer_.start();
        } catch (const std::exception& error) {
            showError(error);
        }
    }

    void showResult()
    {
        if (pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return;
        }
        pollTimer_.stop();

        try {
            const auto round = pending_.get();
            auto line = QStringLiteral("Раунд %1. Волк: %2, козлёнок: %3. "
                                       "Спрятались: %4, попались: %5, мертвы: %6.")
                .arg(++roundNumber_).arg(round.wolfNumber).arg(round.kidNumber)
                .arg(round.hidden()).arg(round.caught()).arg(round.dead());
            if (!round.wasAlive && round.alive) {
                line += QStringLiteral(" Козлёнок воскрес.");
            }
            log_.appendPlainText(line);

            if (round.finished) {
                closeGame(QStringLiteral("Игра завершилась. Козлёнок отключился."));
                return;
            }
            waitForNumber();
        } catch (const std::exception& error) {
            showError(error);
        }
    }

    void showError(const std::exception& error)
    {
        std::cerr << "Хост: " << error.what() << '\n';
        log_.appendPlainText(QString::fromLocal8Bit(error.what()));
        closeGame(QStringLiteral("Связь с козлёнком прервана."));
    }

    void closeGame(const QString& message)
    {
        status_.setText(message);
        log_.appendPlainText(message);
        number_.setEnabled(false);
        submit_.setEnabled(false);
        session_.reset();
    }

    std::unique_ptr<wolf::GameSession> session_;
    std::future<wolf::Round> pending_;
    QLabel status_;
    QPlainTextEdit log_;
    QLineEdit number_;
    QPushButton submit_{QStringLiteral("Сыграть")};
    QTimer inputTimer_;
    QTimer pollTimer_;
    int roundNumber_ = 0;
};

}

int main(int argc, char* argv[])
{
    std::unique_ptr<wolf::GameSession> session;
    QString startupError;
    try {
        // fork выполняется до инициализации Qt и запуска потоков.
        session = std::make_unique<wolf::GameSession>();
    } catch (const std::exception& error) {
        std::cerr << "Хост: " << error.what() << '\n';
        startupError = QString::fromLocal8Bit(error.what());
    }

    QApplication application(argc, argv);
    HostWindow window(std::move(session));
    if (!startupError.isEmpty()) {
        window.showStartupError(startupError);
    }
    window.show();
    const int result = application.exec();
    return startupError.isEmpty() ? result : EXIT_FAILURE;
}
