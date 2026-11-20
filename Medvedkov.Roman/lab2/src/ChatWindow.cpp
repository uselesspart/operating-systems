#include "ChatWindow.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>
#include <unistd.h>

namespace chat {
namespace {
QString name(Party party) {
    if (party == Party::Host) return QStringLiteral("Хост");
    if (party == Party::Client) return QStringLiteral("Клиент");
    return QStringLiteral("Всем");
}
}

ChatWindow::ChatWindow(ChatSession& session, const char* transport) : session_(session) {
    setWindowTitle(QStringLiteral("Локальный чат — %1 [%2]").arg(name(session.role()), QString::fromLatin1(transport)));
    resize(620, 500);
    setStyleSheet("QWidget { font-size: 14px; } QLineEdit, QTextBrowser, QComboBox { padding: 7px; } QPushButton { padding: 8px 16px; }");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);
    auto* heading = new QHBoxLayout;
    auto* title = new QLabel(name(session.role()), this);
    title->setStyleSheet("font-size: 24px; font-weight: 600;");
    heading->addWidget(title);
    heading->addStretch();
    heading->addWidget(new QLabel(QStringLiteral("%1 · PID %2").arg(QString::fromLatin1(transport).toUpper()).arg(getpid()), this));
    layout->addLayout(heading);
    status_ = new QLabel(this);
    status_->setObjectName("statusLabel");
    status_->setWordWrap(true);
    layout->addWidget(status_);
    history_ = new QTextBrowser(this);
    history_->setObjectName("messageHistory");
    history_->setOpenExternalLinks(false);
    layout->addWidget(history_, 1);
    auto* controls = new QHBoxLayout;
    recipient_ = new QComboBox(this);
    recipient_->setObjectName("recipientBox");
    recipient_->addItem(QStringLiteral("Всем"), static_cast<int>(Party::Everyone));
    const auto peer = session.role() == Party::Host ? Party::Client : Party::Host;
    recipient_->addItem(QStringLiteral("Лично: %1").arg(name(peer)), static_cast<int>(peer));
    controls->addWidget(recipient_);
    controls->addStretch();
    counter_ = new QLabel(this);
    counter_->setObjectName("byteCounter");
    controls->addWidget(counter_);
    layout->addLayout(controls);
    auto* editor = new QHBoxLayout;
    input_ = new QLineEdit(this);
    input_->setObjectName("messageInput");
    input_->setMaxLength(static_cast<int>(Message::MaxText));
    input_->setPlaceholderText(QStringLiteral("Введите сообщение…"));
    send_ = new QPushButton(QStringLiteral("Отправить"), this);
    send_->setObjectName("sendButton");
    editor->addWidget(input_, 1);
    editor->addWidget(send_);
    layout->addLayout(editor);
    connect(input_, &QLineEdit::textChanged, this, [this] { refreshInput(); });
    connect(input_, &QLineEdit::returnPressed, this, [this] { sendInput(); });
    connect(send_, &QPushButton::clicked, this, [this] { sendInput(); });
    session_.onMessage = [this](const Message& message) { showMessage(message); };
    session_.onStatus = [this](const std::string& text) { showStatus(QString::fromStdString(text)); };
    session_.onDisconnected = [this] {
        refreshInput();
        if (session_.role() == Party::Client) QTimer::singleShot(0, qApp, &QApplication::quit);
    };
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, [this] { session_.tick(); });
    timer_->start(20);
    showStatus(QStringLiteral("Соединение установлено · собеседник PID %1").arg(session.peer()));
    refreshInput();
    input_->setFocus();
}

void ChatWindow::refreshInput() {
    const auto bytes = input_->text().toUtf8().size();
    counter_->setText(QStringLiteral("%1 / %2 байт").arg(bytes).arg(Message::MaxText));
    input_->setEnabled(session_.connected());
    recipient_->setEnabled(session_.connected());
    send_->setEnabled(session_.connected() && !input_->text().trimmed().isEmpty() && bytes <= static_cast<int>(Message::MaxText));
}

void ChatWindow::sendInput() {
    if (!send_->isEnabled()) return;
    try {
        session_.submit(input_->text().toUtf8().toStdString(), static_cast<Party>(recipient_->currentData().toInt()));
        input_->clear();
    } catch (const std::exception& error) {
        showStatus(QString::fromUtf8(error.what()));
    }
}

void ChatWindow::showMessage(const Message& message) {
    const auto time = QDateTime::fromMSecsSinceEpoch(message.sentAtNs / 1000000).toString("HH:mm:ss.zzz");
    const auto text = QString::fromUtf8(message.text.data(), static_cast<int>(message.length)).toHtmlEscaped();
    const auto color = message.recipient == Party::Everyone ? "#253b53" : "#7040a0";
    history_->append(QStringLiteral("<p style='color:%1'><b>#%2 · %3 · %4 → %5</b><br>%6</p>")
                         .arg(QString::fromLatin1(color)).arg(message.sequence).arg(time, name(message.sender), name(message.recipient), text));
}

void ChatWindow::showStatus(const QString& text) {
    status_->setText(text);
    status_->setStyleSheet(session_.connected() ? "color: #236746;" : "color: #9c3030;");
    history_->append(QStringLiteral("<p style='color:#687786'><i>%1</i></p>").arg(text.toHtmlEscaped()));
}

void ChatWindow::closeEvent(QCloseEvent* event) {
    timer_->stop();
    session_.disconnect(session_.role() == Party::Host ? "Host closed the window" : "Client closed the window", SIGTERM);
    QWidget::closeEvent(event);
}
}
