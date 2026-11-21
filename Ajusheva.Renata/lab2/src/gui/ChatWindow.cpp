#include "gui/ChatWindow.hpp"

#include "chat/Render.hpp"

#include <QComboBox>
#include <QDateTime>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>

namespace chatlab {

namespace {

QString toQt(const std::string& text) {
    return QString::fromStdString(text);
}

QGroupBox* framed(const QString& title, QWidget* content) {
    auto* box = new QGroupBox(title);
    auto* layout = new QVBoxLayout(box);
    layout->addWidget(content);
    return box;
}

}

ChatWindow::ChatWindow(const QString& kind, qint64 pid, QWidget* parent)
    : QMainWindow(parent), kind_(kind), pid_(pid) {
    setWindowTitle(QString("Локальный чат: хост (%1, pid %2)").arg(kind).arg(pid));
    resize(900, 600);

    feed_ = new QPlainTextEdit;
    feed_->setObjectName("feed");
    feed_->setReadOnly(true);

    journal_ = new QPlainTextEdit;
    journal_->setObjectName("journal");
    journal_->setReadOnly(true);
    journal_->setMaximumBlockCount(2000);

    members_ = new QListWidget;
    members_->setObjectName("members");

    target_ = new QComboBox;
    target_->setObjectName("target");
    target_->addItem("Всем", kEveryone);

    input_ = new QLineEdit;
    input_->setObjectName("input");
    input_->setPlaceholderText("Сообщение");

    send_ = new QPushButton("Отправить");
    send_->setObjectName("send");

    counter_ = new QLabel;
    counter_->setObjectName("counter");

    auto* inputRow = new QHBoxLayout;
    inputRow->addWidget(new QLabel("Кому:"));
    inputRow->addWidget(target_);
    inputRow->addWidget(input_, 1);
    inputRow->addWidget(send_);

    auto* chatSide = new QWidget;
    auto* chatLayout = new QVBoxLayout(chatSide);
    chatLayout->setContentsMargins(0, 0, 0, 0);
    chatLayout->addWidget(framed("Чат", feed_), 1);
    chatLayout->addLayout(inputRow);

    auto* upper = new QSplitter(Qt::Horizontal);
    upper->addWidget(chatSide);
    upper->addWidget(framed("Участники", members_));
    upper->setStretchFactor(0, 3);
    upper->setStretchFactor(1, 1);

    auto* whole = new QSplitter(Qt::Vertical);
    whole->addWidget(upper);
    whole->addWidget(framed("События", journal_));
    whole->setStretchFactor(0, 3);
    whole->setStretchFactor(1, 1);
    setCentralWidget(whole);

    statusBar()->addPermanentWidget(counter_);
    refreshCounter();

    connect(send_, &QPushButton::clicked, this, [this] { submitInput(); });
    connect(input_, &QLineEdit::returnPressed, this, [this] { submitInput(); });
}

void ChatWindow::later(std::function<void()> action) {
    QMetaObject::invokeMethod(this, std::move(action), Qt::QueuedConnection);
}

void ChatWindow::onStatus(const std::string& text) {
    const QString line = toQt(text);
    later([this, line] { appendStatus(line); });
}

void ChatWindow::onRoster(const std::vector<Member>& roster) {
    later([this, roster] { applyRoster(roster); });
}

void ChatWindow::onPost(const Post& post) {
    later([this, post] { appendPost(post); });
}

void ChatWindow::onQuitRequest() {
    later([this] {
        appendStatus("Получен сигнал завершения");
        close();
    });
}

void ChatWindow::submitInput() {
    const QString text = input_->text().trimmed();
    if (text.isEmpty()) {
        return;
    }
    if (sendHandler_) {
        sendHandler_(target_->currentData().toInt(), text.toStdString());
    }
    input_->clear();
}

void ChatWindow::appendStatus(const QString& text) {
    const QString stamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    journal_->appendPlainText("[" + stamp + "] " + text);
    statusBar()->showMessage(text, 5000);
}

void ChatWindow::appendPost(const Post& post) {
    feed_->appendPlainText(toQt(render::postLine(post, kHostId)));
}

void ChatWindow::applyRoster(const std::vector<Member>& roster) {
    const int chosen = target_->currentData().toInt();
    members_->clear();
    target_->clear();
    target_->addItem("Всем", kEveryone);
    clients_ = 0;
    for (const Member& member : roster) {
        const QString label = QString("%1 %2").arg(member.id).arg(toQt(member.name));
        members_->addItem(label);
        if (member.id == kHostId) {
            continue;
        }
        ++clients_;
        target_->addItem(label, member.id);
    }
    const int index = target_->findData(chosen);
    target_->setCurrentIndex(index >= 0 ? index : 0);
    refreshCounter();
}

void ChatWindow::refreshCounter() {
    counter_->setText(QString("тип связи: %1 | pid: %2 | клиентов: %3").arg(kind_).arg(pid_).arg(clients_));
}

}
