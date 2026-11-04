#include "chat_window.h"
#include "chat.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

ChatWindow::ChatWindow(const QString& role, const QString& type, pid_t host_pid,
                       QWidget* parent)
    : QWidget(parent) {
    setWindowTitle(QString("Chat - %1 (%2)").arg(role, type));
    resize(520, 360);

    auto* layout = new QVBoxLayout(this);
    auto* info = new QFormLayout;
    auto* role_label = new QLabel(role, this);
    auto* type_label = new QLabel(type, this);
    auto* pid_label = new QLabel(QString::number(host_pid), this);
    role_label->setObjectName("role");
    type_label->setObjectName("type");
    pid_label->setObjectName("host_pid");
    info->addRow("Role:", role_label);
    info->addRow("IPC:", type_label);
    info->addRow("Host PID:", pid_label);
    layout->addLayout(info);

    history = new QListWidget(this);
    history->setObjectName("history");
    layout->addWidget(history);

    recipient = new QComboBox(this);
    recipient->setObjectName("recipient");
    recipient->addItem("Everyone", -1);
    recipient->addItem("Host", 0);
    recipient->addItem("Client", 1);
    recipient->setEnabled(false);
    layout->addWidget(recipient);

    input = new QLineEdit(this);
    input->setObjectName("text");
    input->setPlaceholderText("Message");
    input->setEnabled(false);
    layout->addWidget(input);

    send = new QPushButton("Send", this);
    send->setObjectName("send");
    send->setEnabled(false);
    layout->addWidget(send);

    status = new QLabel("Not connected", this);
    status->setObjectName("status");
    layout->addWidget(status);
    connect(send, &QPushButton::clicked, this, &ChatWindow::submit);
    connect(input, &QLineEdit::returnPressed, this, &ChatWindow::submit);
}

void ChatWindow::set_status(const QString& text) { status->setText(text); }

void ChatWindow::set_connected(bool connected) {
    input->setEnabled(connected);
    send->setEnabled(connected);
    recipient->setEnabled(connected);
}

void ChatWindow::clear_input() {
    input->clear();
    set_status("Connected");
}

void ChatWindow::submit() {
    if (!send->isEnabled()) {
        return;
    }
    Message message;
    message.recipient = recipient->currentData().toInt();
    message.kind = message.recipient == -1 ? MessageKind::Public : MessageKind::Private;
    message.number = 1;
    message.text = input->text().toUtf8().toStdString();
    try {
        Chat::validate(message);
    } catch (const std::exception& error) {
        set_status(QString::fromUtf8(error.what()));
        return;
    }
    emit send_requested(message.recipient, input->text());
}

void ChatWindow::set_history(const History& messages) {
    history->clear();
    for (const auto& message : messages) {
        QString author = message.sender == 0 ? "Host" : "Client";
        if (message.kind == MessageKind::Private) {
            author += message.recipient == 0 ? " -> Host (private)" : " -> Client (private)";
        }
        history->addItem(author + ": " + QString::fromUtf8(message.text.data(), message.text.size()));
    }
    history->scrollToBottom();
}
