#include "window.hpp"
#include <QApplication>
#include <QDateTime>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <algorithm>

namespace lab {
Window::Window(Session& session) : session_(session) {
    setWindowTitle(QString::fromUtf8("Волк и козлёнок — вариант 1 · ") + TYPE_CODE);
    resize(1040, 720);
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    auto* title = new QLabel(QString::fromUtf8("Волк и семеро козлят"), central);
    title->setStyleSheet("font-size: 26px; font-weight: bold; color: #26334d; padding: 8px 0;");
    layout->addWidget(title);
    layout->addWidget(new QLabel(QString::fromUtf8("Вариант 1: один хост-волк и один дочерний процесс-козлёнок (n = 1). Связь: ") + TYPE_CODE, central));
    layout->addWidget(new QLabel(QString::fromUtf8("Живой: 1–100, спрятался при |волк − козлёнок| ≤ 70. Мёртвый: 1–50, воскресает при разнице ≤ 20."), central));
    status_ = new QLabel(QString::fromUtf8("Ожидание подключения козлёнка…"), central);
    status_->setObjectName("status");
    status_->setStyleSheet("font-size: 17px; padding: 10px; background: #edf2fa; border-radius: 6px;");
    layout->addWidget(status_);
    goat_ = new QLabel(QString::fromUtf8("Козлёнок: ожидает подключения"), central);
    layout->addWidget(goat_);
    auto* input = new QHBoxLayout;
    input->addWidget(new QLabel(QString::fromUtf8("Число волка:"), central));
    number_ = new QSpinBox(central);
    number_->setObjectName("wolfNumber");
    number_->setRange(1, 100);
    number_->setValue(50);
    input->addWidget(number_);
    submit_ = new QPushButton(QString::fromUtf8("Сделать ход"), central);
    submit_->setObjectName("submitTurn");
    submit_->setEnabled(false);
    input->addWidget(submit_);
    countdown_ = new QLabel(central);
    input->addWidget(countdown_, 1);
    layout->addLayout(input);
    layout->addWidget(new QLabel(QString::fromUtf8("Введите число и нажмите «Сделать ход» или Enter. Через 3 секунды число выбирается автоматически."), central));
    table_ = new QTableWidget(0, 8, central);
    table_->setObjectName("rounds");
    table_->setHorizontalHeaderLabels({QString::fromUtf8("Ход"), QString::fromUtf8("Волк"), QString::fromUtf8("Козлёнок"),
        QString::fromUtf8("Спрятались"), QString::fromUtf8("Попались"), QString::fromUtf8("Воскресли"),
        QString::fromUtf8("Мертвы"), QString::fromUtf8("Состояние")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    layout->addWidget(table_, 3);
    events_ = new QTextEdit(central);
    events_->setReadOnly(true);
    events_->setObjectName("events");
    layout->addWidget(events_, 1);
    auto* close = new QPushButton(QString::fromUtf8("Завершить и закрыть"), central);
    QObject::connect(close, &QPushButton::clicked, this, &QWidget::close);
    layout->addWidget(close);
    setCentralWidget(central);
    QObject::connect(submit_, &QPushButton::clicked, this, [this] { submit(number_->value()); });
    QObject::connect(number_, &QSpinBox::editingFinished, this, [this] {
        if (accepting_ && number_->hasFocus()) submit(number_->value());
    });
    timer_.setInterval(40);
    QObject::connect(&timer_, &QTimer::timeout, this, [this] {
        const qint64 left = std::max<qint64>(0, 3000 - deadline_.elapsed());
        countdown_->setText(QString::fromUtf8("Автовыбор через %1 с").arg(static_cast<double>(left) / 1000.0, 0, 'f', 1));
        if (left == 0) submit(session_.nextWolf());
    });
    signalTimer_.setInterval(100);
    QObject::connect(&signalTimer_, &QTimer::timeout, this, [this] { if (interrupted) this->close(); });
    signalTimer_.start();
    QTimer::singleShot(0, this, [this] { connectClient(); });
}
Window::~Window() {
    timer_.stop();
    joinWorker();
    session_.stop();
}
void Window::joinWorker() { if (worker_.joinable()) worker_.join(); }
void Window::addEvent(const QString& text) {
    events_->append(QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + text.toHtmlEscaped());
}
void Window::connectClient() {
    worker_ = std::thread([this] {
        try {
            session_.waitReady();
            QMetaObject::invokeMethod(this, [this] {
                joinWorker();
                addEvent(QString::fromUtf8("Козлёнок подключился. Игра началась."));
                beginTurn();
            }, Qt::QueuedConnection);
        } catch (const std::exception& error) {
            const QString text = QString::fromUtf8(error.what());
            QMetaObject::invokeMethod(this, [this, text] { showError(text); }, Qt::QueuedConnection);
        }
    });
}
void Window::beginTurn() {
    accepting_ = true;
    submit_->setEnabled(true);
    number_->setEnabled(true);
    goat_->setText(session_.game().alive() ? QString::fromUtf8("Козлёнок: жив") : QString::fromUtf8("Козлёнок: мёртв, может воскреснуть"));
    status_->setText(QString::fromUtf8("Ход %1. Выберите число от 1 до 100.").arg(session_.game().round() + 1));
    number_->setFocus();
    deadline_.start();
    timer_.start();
}
void Window::submit(int wolf) {
    if (!accepting_) return;
    accepting_ = false;
    timer_.stop();
    submit_->setEnabled(false);
    number_->setEnabled(false);
    countdown_->setText(QString::fromUtf8("Обмен с козлёнком…"));
    joinWorker();
    worker_ = std::thread([this, wolf] {
        try {
            const Round round = session_.play(wolf);
            QMetaObject::invokeMethod(this, [this, round] { showRound(round); }, Qt::QueuedConnection);
        } catch (const std::exception& error) {
            const QString text = QString::fromUtf8(error.what());
            QMetaObject::invokeMethod(this, [this, text] { showError(text); }, Qt::QueuedConnection);
        }
    });
}
void Window::showRound(const Round& round) {
    joinWorker();
    const int row = table_->rowCount();
    table_->insertRow(row);
    const QStringList values = {QString::number(round.number), QString::number(round.wolf), QString::number(round.goat),
        QString::number(round.hidden), QString::number(round.caught), QString::number(round.resurrected), QString::number(round.dead),
        round.alive ? QString::fromUtf8("Жив") : QString::fromUtf8("Мёртв")};
    for (int column = 0; column < values.size(); ++column) table_->setItem(row, column, new QTableWidgetItem(values[column]));
    table_->scrollToBottom();
    addEvent(QString::fromUtf8("Ход %1: волк %2, козлёнок %3. %4").arg(round.number).arg(round.wolf).arg(round.goat)
        .arg(round.resurrected ? QString::fromUtf8("Козлёнок воскрес!") : (round.alive ? QString::fromUtf8("Козлёнок спрятался.") : QString::fromUtf8("Козлёнок мёртв."))));
    if (session_.finished()) {
        session_.stop();
        const QString text = round.finished ? QString::fromUtf8("Игра завершена: козлёнок мёртв два хода подряд.")
                                            : QString::fromUtf8("Достигнут заданный лимит ходов.");
        status_->setText(text);
        goat_->setText(round.alive ? QString::fromUtf8("Козлёнок: жив") : QString::fromUtf8("Козлёнок: мёртв"));
        countdown_->clear();
        addEvent(text + QString::fromUtf8(" Клиент завершился, связь закрыта."));
        if (session_.options().exitOnFinish) QTimer::singleShot(0, this, &QWidget::close);
    } else beginTurn();
}
void Window::showError(const QString& error) {
    joinWorker();
    failed_ = true;
    accepting_ = false;
    timer_.stop();
    submit_->setEnabled(false);
    number_->setEnabled(false);
    countdown_->clear();
    status_->setText(QString::fromUtf8("Связь прервана: ") + error);
    addEvent(status_->text());
    session_.logger().write("host error: " + error.toStdString());
    session_.stop();
    if (session_.options().exitOnFinish) QTimer::singleShot(0, this, &QWidget::close);
}
}
