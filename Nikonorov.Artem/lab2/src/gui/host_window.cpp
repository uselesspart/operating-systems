#include "host_window.hpp"

#include "../host_worker.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cerrno>
#include <cstdlib>
#include <random>
#include <stdexcept>
#include <sys/wait.h>

HostWindow::HostWindow(Conn* connection, pid_t client_pid)
    : connection_(connection),
      worker_(new HostWorker(connection)),
      worker_thread_(new QThread(this)),
      child_watch_timer_(new QTimer(this)),
      countdown_timer_(new QTimer(this)),
      connection_label_(new QLabel("Соединение: ожидаем первый ответ козлёнка")),
      status_label_(new QLabel("Хост готов к игре")),
      round_label_(new QLabel("Подготовка первого раунда")),
      wolf_input_(new QSpinBox),
      throw_button_(new QPushButton("Бросить число")),
      log_view_(new QTextEdit),
      game_(),
      client_pid_(client_pid),
      current_round_(1),
      current_wolf_number_(0),
      countdown_seconds_(3),
      round_started_(false),
      game_over_(false),
      client_reaped_(false) {
    setWindowTitle("Волк и семеро козлят");
    resize(720, 500);

    wolf_input_->setRange(1, 100);
    wolf_input_->setEnabled(false);
    wolf_input_->setKeyboardTracking(false);
    throw_button_->setEnabled(false);
    log_view_->setReadOnly(true);
    countdown_timer_->setInterval(1000);

    auto* input_layout = new QHBoxLayout;
    input_layout->addWidget(new QLabel("Число волка:"));
    input_layout->addWidget(wolf_input_);
    input_layout->addWidget(throw_button_);

    auto* layout = new QVBoxLayout;
    layout->addWidget(connection_label_);
    layout->addWidget(status_label_);
    layout->addWidget(round_label_);
    layout->addLayout(input_layout);
    layout->addWidget(log_view_);

    auto* central = new QWidget;
    central->setLayout(layout);
    setCentralWidget(central);

    worker_->moveToThread(worker_thread_);
    connect(worker_thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &HostWorker::clientConnected, this, [this]() {
        connection_label_->setText("Соединение: козлёнок подключился");
        appendLog("Козлёнок подключился (PID " + QString::number(client_pid_) + ")");
    });
    connect(worker_, &HostWorker::goatReceived, this, [this](int round, int value) {
        handleGoat(round, value);
    });
    connect(worker_, &HostWorker::resultSent, this, [this](int round, bool alive, bool game_over) {
        handleResult(round, alive, game_over);
    });
    connect(worker_, &HostWorker::failed, this, [this](const QString& message) {
        countdown_timer_->stop();
        status_label_->setText("Ошибка взаимодействия с дочерним процессом");
        round_label_->setText(message);
        appendLog("Ошибка: " + message);
        wolf_input_->setEnabled(false);
        throw_button_->setEnabled(false);
    });
    connect(throw_button_, &QPushButton::clicked, this, [this]() {
        startRound(wolf_input_->value());
    });
    connect(wolf_input_, &QSpinBox::editingFinished, this, [this]() {
        if (wolf_input_->isEnabled()) {
            startRound(wolf_input_->value());
        }
    });
    connect(countdown_timer_, &QTimer::timeout, this, [this]() {
        if (round_started_ || game_over_) {
            countdown_timer_->stop();
            return;
        }
        --countdown_seconds_;
        if (countdown_seconds_ <= 0) {
            countdown_timer_->stop();
            static std::mt19937 generator(std::random_device{}());
            std::uniform_int_distribution<int> distribution(1, 100);
            appendLog("Время ввода истекло - волк выбрал число случайно");
            startRound(distribution(generator));
            return;
        }
        round_label_->setText(
            "Раунд " + QString::number(current_round_)
            + ": введите число или нажмите Enter (" + QString::number(countdown_seconds_)
            + " сек.)");
    });

    worker_thread_->start();
    child_watch_timer_->setInterval(200);
    connect(child_watch_timer_, &QTimer::timeout, this, [this]() {
        checkChildProcess();
    });
    child_watch_timer_->start();
    appendLog("Хост запущен. PID козлёнка: " + QString::number(client_pid_));

    QTimer::singleShot(0, this, [this]() {
        prepareRound();
    });
}

HostWindow::~HostWindow() {
    child_watch_timer_->stop();
    countdown_timer_->stop();
    connection_->Cancel();
    worker_thread_->quit();
    worker_thread_->wait();
    worker_ = nullptr;
}

void HostWindow::prepareRound() {
    if (game_over_ || client_reaped_) {
        return;
    }
    round_started_ = false;
    countdown_seconds_ = 3;
    round_label_->setText("Раунд " + QString::number(current_round_)
                          + ": введите число или нажмите Enter (3 сек.)");
    wolf_input_->setEnabled(true);
    throw_button_->setEnabled(true);
    countdown_timer_->start();
}

void HostWindow::startRound(int wolf_number) {
    if (round_started_ || game_over_) {
        return;
    }
    if (wolf_number < 1 || wolf_number > 100) {
        appendLog("Ошибка: число волка должно быть от 1 до 100");
        return;
    }

    countdown_timer_->stop();
    round_started_ = true;
    wolf_input_->setEnabled(false);
    throw_button_->setEnabled(false);
    current_wolf_number_ = wolf_number;
    appendLog("Раунд " + QString::number(current_round_)
              + ": волк выбросил " + QString::number(wolf_number));
    round_label_->setText("Ожидание числа козлёнка");
    const int maximum = game_.IsAlive() ? 100 : 50;
    QMetaObject::invokeMethod(worker_, "requestGoat", Qt::QueuedConnection,
                              Q_ARG(int, current_round_), Q_ARG(int, maximum));
}

void HostWindow::handleGoat(int round, int goat_number) {
    if (round != current_round_ || game_over_) {
        return;
    }

    try {
        const RoundResult result = game_.ProcessRound(current_wolf_number_, goat_number);
        if (result.game_over) {
            game_over_ = true;
        }
        const QString line = "Раунд " + QString::number(round)
            + ": волк " + QString::number(current_wolf_number_)
            + ", козлёнок " + QString::number(goat_number)
            + "; спрятались: " + QString::number(result.hidden ? 1 : 0)
            + ", попались: " + QString::number(result.caught ? 1 : 0)
            + ", воскресли: " + QString::number(result.revived ? 1 : 0)
            + ", мёртвые: " + QString::number(result.dead_goats);
        appendLog(line);

        QString goat_status;
        if (result.caught) {
            goat_status = "попался";
        } else if (result.revived) {
            goat_status = "воскрес";
        } else if (result.hidden) {
            goat_status = "спрятался";
        } else {
            goat_status = "остаётся мёртвым";
        }
        status_label_->setText("Козлёнок " + goat_status);

        QMetaObject::invokeMethod(worker_, "sendResult", Qt::QueuedConnection,
                                  Q_ARG(int, round), Q_ARG(bool, result.alive),
                                  Q_ARG(bool, result.game_over));
    } catch (const std::exception& exception) {
        status_label_->setText("Ошибка игровой логики");
        appendLog("Ошибка: " + QString::fromUtf8(exception.what()));
    }
}

void HostWindow::handleResult(int round, bool alive, bool game_over) {
    if (round != current_round_) {
        return;
    }
    if (game_over) {
        game_over_ = true;
        status_label_->setText("Игра завершена");
        round_label_->setText("Все козлята мертвы два раунда подряд");
        appendLog("Игра завершена: два раунда подряд без живых козлят");
        return;
    }

    status_label_->setText(alive ? "Козлёнок жив" : "Козлёнок мёртв");
    ++current_round_;
    QTimer::singleShot(300, this, [this]() {
        prepareRound();
    });
}

void HostWindow::checkChildProcess() {
    if (client_reaped_) {
        return;
    }

    int status = 0;
    const pid_t result = waitpid(client_pid_, &status, WNOHANG);
    if (result == 0 || (result == -1 && errno == EINTR)) {
        return;
    }
    if (result == -1 && errno == ECHILD) {
        client_reaped_ = true;
        child_watch_timer_->stop();
        return;
    }
    if (result == -1) {
        appendLog("Ошибка waitpid при проверке дочернего процесса");
        child_watch_timer_->stop();
        return;
    }

    client_reaped_ = true;
    child_watch_timer_->stop();
    countdown_timer_->stop();
    if (WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS) {
        appendLog("Дочерний процесс завершился штатно");
        if (!game_over_) {
            status_label_->setText("Козлёнок завершился раньше окончания игры");
        }
    } else {
        appendLog("Дочерний процесс завершился аварийно");
        status_label_->setText("Дочерний процесс завершился с ошибкой");
        wolf_input_->setEnabled(false);
        throw_button_->setEnabled(false);
    }
}

void HostWindow::appendLog(const QString& message) {
    log_view_->append(message);
}
