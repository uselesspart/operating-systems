#include "WolfInputDialog.h"
#include <QVBoxLayout>
#include <QMessageBox>
#include <QIntValidator>
#include <QCloseEvent>
#include <cstdlib>

WolfInputDialog::WolfInputDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Ход волка");
    setModal(true);
    // Убираем крестик из заголовка
    setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("Введите число от 1 до 100:", this));

    edit_ = new QLineEdit(this);
    edit_->setValidator(new QIntValidator(1, 100, edit_));
    layout->addWidget(edit_);

    countdown_ = new QLabel("Осталось: 3 сек", this);
    layout->addWidget(countdown_);

    submit_ = new QPushButton("Отправить", this);
    layout->addWidget(submit_);

    timer_ = new QTimer(this);
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, &WolfInputDialog::onTick);
    connect(submit_, &QPushButton::clicked, this, &WolfInputDialog::onSubmit);
    connect(edit_, &QLineEdit::returnPressed, this, &WolfInputDialog::onSubmit);

    timer_->start();
    edit_->setFocus();
}

void WolfInputDialog::closeEvent(QCloseEvent* event) {
    // Закрывать окно крестиком нельзя - только accept() из finishWith()
    event->ignore();
}

void WolfInputDialog::reject() {
    // Esc тоже не должен закрывать
}

void WolfInputDialog::onTick() {
    remaining_--;
    countdown_->setText(QString("Осталось: %1 сек").arg(remaining_));
    if (remaining_ <= 0) finishWith(1 + std::rand() % 100);
}

void WolfInputDialog::onSubmit() {
    bool ok = false;
    int v = edit_->text().toInt(&ok);
    if (!ok || v < 1 || v > 100) {
        QMessageBox::warning(this, "Ошибка", "Введите целое число от 1 до 100");
        return;
    }
    finishWith(v);
}

void WolfInputDialog::finishWith(int v) {
    value_ = v;
    timer_->stop();
    accept();
}