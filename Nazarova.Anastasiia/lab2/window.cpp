#include "window.hpp"
#include "chat.hpp"

#include <QComboBox>
#include <QDateTime>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QListWidget>
#include <QVBoxLayout>

class MessageItem final : public QListWidgetItem {
public:
    explicit MessageItem(QJsonObject message) : message_(std::move(message)) {}
    bool operator<(const QListWidgetItem& other) const override {
        return earlierMessage(message_, static_cast<const MessageItem&>(other).message_);
    }
private:
    QJsonObject message_;
};

ChatWindow::ChatWindow(pid_t host, pid_t self) : host_(host), self_(self) {
    setWindowTitle(QString("Чат (%1), PID %2").arg(TYPE_CODE).arg(self_));
    auto* layout = new QVBoxLayout(this);
    status_ = new QLabel("Подключение...", this);
    status_->setObjectName("status");
    status_->setWordWrap(true);
    layout->addWidget(status_);
    messages_ = new QListWidget(this);
    messages_->setObjectName("messages");
    messages_->setWordWrap(true);
    messages_->setTextElideMode(Qt::ElideNone);
    messages_->setSortingEnabled(true);
    messages_->setResizeMode(QListView::Adjust);
    layout->addWidget(messages_);
    auto* row = new QHBoxLayout;
    recipient_ = new QComboBox(this);
    recipient_->setObjectName("recipient");
    recipient_->addItem("Всем", 0);
    row->addWidget(recipient_);
    text_ = new QLineEdit(this);
    text_->setObjectName("messageText");
    text_->setPlaceholderText("Сообщение");
    text_->setEnabled(false);
    row->addWidget(text_, 1);
    send_ = new QPushButton("Отправить", this);
    send_->setObjectName("send");
    send_->setEnabled(false);
    row->addWidget(send_);
    layout->addLayout(row);
    connect(send_, &QPushButton::clicked, this, [this] { send(); });
    connect(text_, &QLineEdit::returnPressed, this, [this] { send(); });
    if (const auto* screen = QGuiApplication::primaryScreen()) {
        resize(sizeHint().expandedTo(screen->availableGeometry().size() / 2));
    }
}

QString ChatWindow::participant(pid_t pid) const {
    if (pid == 0) return "Всем";
    if (pid == host_) return QString("Хост (%1)").arg(pid);
    return QString("Клиент %1").arg(pid);
}

void ChatWindow::SetSender(std::function<void(pid_t, const QString&)> sender) {
    sender_ = std::move(sender);
}

void ChatWindow::send() {
    if (!sender_ || text_->text().trimmed().isEmpty()) return;
    try {
        sender_(recipient_->currentData().toInt(), text_->text());
        text_->clear();
    } catch (const std::exception& error) {
        status_->setText(QString::fromUtf8(error.what()));
    }
}

void ChatWindow::Handle(const QJsonObject& event) {
    const auto type = event["type"].toString();
    if (type == "chat") {
        auto* item = new MessageItem(event);
        const auto time = event["time"].toString().toLongLong();
        item->setText(QString("%1  %2, кому: %3\n%4")
            .arg(QDateTime::fromMSecsSinceEpoch(time / 1000).toString("HH:mm:ss.zzz"),
                 participant(event["sender"].toInt()), participant(event["recipient"].toInt()),
                 event["text"].toString()));
        messages_->addItem(item);
        messages_->scrollToItem(item);
    } else if (type == "members") {
        const int selected = recipient_->currentData().toInt();
        recipient_->clear();
        recipient_->addItem("Всем", 0);
        for (const auto& member : event["members"].toArray()) {
            if (member.toInt() != self_) recipient_->addItem(participant(member.toInt()), member.toInt());
        }
        const int index = recipient_->findData(selected);
        recipient_->setCurrentIndex(index < 0 ? 0 : index);
        status_->setText(event["text"].toString());
        text_->setEnabled(true);
        send_->setEnabled(true);
    } else {
        status_->setText(event["text"].toString());
        if (type == "closed") {
            text_->setEnabled(false);
            send_->setEnabled(false);
        }
    }
}
