#include CONN_HEADER
#include "game.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    if (sigprocmask(SIG_BLOCK, &mask, nullptr) < 0) {
        perror("sigprocmask");
        return 1;
    }
    int signals_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (signals_fd < 0) {
        perror("signalfd");
        return 1;
    }

    QApplication app(argc, argv);
    Session session;
    Conn conn;
    if (!session.open(getpid(), true) || !conn.open(session, true)) {
        perror("Создание канала");
        return 1;
    }
    srand(time(nullptr) ^ getpid());

    QWidget window;
    QVBoxLayout layout(&window);
    QLabel status;
    QPlainTextEdit log;
    QLineEdit input;
    QPushButton submit("Сделать ход");
    window.setWindowTitle(QString("Волк и семеро козлят — PID %1").arg(getpid()));
    log.setReadOnly(true);
    input.setPlaceholderText("Число волка от 1 до 100");
    layout.addWidget(&status);
    layout.addWidget(&log);
    layout.addWidget(&input);
    layout.addWidget(&submit);
    window.resize(560, 340);

    QTimer poll;
    QTimer turn;
    QElapsedTimer waiting;
    turn.setSingleShot(true);
    turn.setTimerType(Qt::PreciseTimer);
    Game game = {true, 0, 0};
    bool connected = false;
    bool finished = false;
    int goat = 0;

    auto enable_input = [&](bool enabled) {
        input.setEnabled(enabled);
        submit.setEnabled(enabled);
    };
    auto finish = [&](const QString &message) {
        finished = true;
        turn.stop();
        enable_input(false);
        status.setText(message);
        log.appendPlainText(message);
    };
    auto fail = [&](const char *operation) {
        int error = errno;
        perror(operation);
        finish(QString("%1: %2").arg(operation, strerror(error)));
    };
    auto play = [&](int wolf) {
        bool was_alive = game.alive;
        play_round(&game, wolf, goat);
        log.appendPlainText(QString("Ход %1. Волк %2, козлёнок %3. "
                                    "Спрятались: %4, попались: %5, мертвы: %6. %7")
            .arg(game.round).arg(wolf).arg(goat).arg(int(was_alive && game.alive))
            .arg(int(was_alive && !game.alive)).arg(int(!game.alive))
            .arg(!was_alive && game.alive ? "Козлёнок воскрес." : ""));
        enable_input(false);
        int reply = game.dead_rounds == 2 ? -1 : int(game.alive);
        if (!conn.write(reply) || sem_post(session.reply) < 0)
            fail("Отправка результата");
        else if (reply == -1)
            finish("Игра окончена. Козлёнок мёртв два хода подряд.");
        else {
            status.setText("Ожидание хода козлёнка");
            waiting.restart();
        }
    };

    QObject::connect(&turn, &QTimer::timeout, [&]() { play(roll(100)); });
    QObject::connect(&submit, &QPushButton::clicked, [&]() {
        bool ok;
        int value = input.text().toInt(&ok);
        if (!ok || value < 1 || value > 100) {
            status.setText("Введите целое число от 1 до 100");
            return;
        }
        turn.stop();
        play(value);
    });
    QObject::connect(&input, &QLineEdit::returnPressed, &submit, &QPushButton::click);
    QObject::connect(&poll, &QTimer::timeout, [&]() {
        signalfd_siginfo info;
        while (read(signals_fd, &info, sizeof(info)) == sizeof(info)) {
            if (info.ssi_signo != SIGUSR1) {
                app.quit();
                return;
            }
            if (!connected) {
                connected = true;
                log.appendPlainText(QString("Подключился козлёнок, PID %1").arg(info.ssi_pid));
                waiting.start();
            }
        }
        if (!connected || finished || turn.isActive())
            return;
        if (sem_trywait(session.request) < 0) {
            if (errno == EAGAIN && waiting.elapsed() < 5000)
                return;
            if (errno == EAGAIN)
                errno = ETIMEDOUT;
            fail("Ожидание козлёнка");
            return;
        }
        if (!conn.read(&goat)) {
            fail("Чтение хода");
            return;
        }
        if (goat < 1 || goat > (game.alive ? 100 : 50)) {
            finish("Получено недопустимое число козлёнка");
            return;
        }
        input.clear();
        enable_input(true);
        input.setFocus();
        status.setText("Ход волка. Через 3 секунды число выпадет автоматически.");
        turn.start(3000);
    });

    enable_input(false);
    status.setText("Ожидание подключения козлёнка");
    printf("PID хоста: %d\n", getpid());
    fflush(stdout);
    poll.start(20);
    window.show();
    int result = app.exec();
    close(signals_fd);
    return result;
}
