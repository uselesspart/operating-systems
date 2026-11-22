#include "gui/HostWindow.h"

#include "chat/Text.h"
#include "util/Clock.h"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>

namespace chat {

namespace {

constexpr int kSentAtRole = Qt::UserRole;
constexpr int kSeqRole = Qt::UserRole + 1;
constexpr int kMaxEvents = 1000;

QGroupBox* framed(const QString& title, QWidget* content)
{
    auto* box = new QGroupBox(title);
    auto* layout = new QVBoxLayout(box);
    layout->addWidget(content);
    return box;
}

bool itemSentAfter(const QListWidgetItem* item, const ChatMessage& message)
{
    const auto sentAt = item->data(kSentAtRole).toLongLong();
    const auto seq = item->data(kSeqRole).toULongLong();
    return sentAt != message.sentAtMs ? sentAt > message.sentAtMs : seq > message.seq;
}

}

HostWindow::HostWindow(const QString& connType, qint64 hostPid, QWidget* parent)
    : QMainWindow(parent), chat_(new QListWidget), participants_(new QListWidget),
      events_(new QPlainTextEdit), recipient_(new QComboBox), input_(new QLineEdit),
      sendButton_(new QPushButton(QStringLiteral("Отправить")))
{
    setWindowTitle(QStringLiteral("Чат: хост (%1), pid %2").arg(connType).arg(hostPid));
    resize(960, 600);

    chat_->setObjectName("chat");
    chat_->setWordWrap(true);
    chat_->setSelectionMode(QAbstractItemView::NoSelection);
    participants_->setObjectName("participants");
    events_->setObjectName("events");
    events_->setReadOnly(true);
    events_->setMaximumBlockCount(kMaxEvents);
    recipient_->setObjectName("recipient");
    recipient_->addItem(QStringLiteral("Всем"), kEveryone);
    input_->setObjectName("input");
    input_->setPlaceholderText(QStringLiteral("Сообщение"));
    sendButton_->setObjectName("send");

    auto* inputRow = new QHBoxLayout;
    inputRow->addWidget(recipient_);
    inputRow->addWidget(input_, 1);
    inputRow->addWidget(sendButton_);

    auto* chatPane = new QWidget;
    auto* chatLayout = new QVBoxLayout(chatPane);
    chatLayout->addWidget(framed(QStringLiteral("Чат"), chat_), 1);
    chatLayout->addLayout(inputRow);

    auto* sidePane = new QSplitter(Qt::Vertical);
    sidePane->addWidget(framed(QStringLiteral("Участники"), participants_));
    sidePane->addWidget(framed(QStringLiteral("События"), events_));

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(chatPane);
    splitter->addWidget(sidePane);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    setCentralWidget(splitter);

    statusBar()->showMessage(
        QStringLiteral("Запуск клиента: ./client_%1 %2 [имя]").arg(connType).arg(hostPid));

    connect(sendButton_, &QPushButton::clicked, this, &HostWindow::sendCurrentMessage);
    connect(input_, &QLineEdit::returnPressed, this, &HostWindow::sendCurrentMessage);
    input_->setFocus();
}

void HostWindow::onStatus(const std::string& text)
{
    inGuiThread([this, status = QString::fromStdString(text)] { showStatus(status); });
}

void HostWindow::onParticipantsChanged(const std::vector<Participant>& participants)
{
    inGuiThread([this, participants] { showParticipants(participants); });
}

void HostWindow::onMessage(const ChatMessage& message)
{
    inGuiThread([this, message] { showMessage(message); });
}

void HostWindow::onShutdownRequested()
{
    inGuiThread([this] {
        showStatus(QStringLiteral("Получен сигнал завершения"));
        close();
    });
}

void HostWindow::sendCurrentMessage()
{
    const QString text = input_->text().trimmed();
    if (text.isEmpty() || controller_ == nullptr) {
        return;
    }
    controller_->sendMessage(recipient_->currentData().toInt(), text.toStdString());
    input_->clear();
}

void HostWindow::showStatus(const QString& text)
{
    events_->appendPlainText(QString::fromStdString(formatTime(wallClockMs())) + "  " + text);
}

void HostWindow::showMessage(const ChatMessage& message)
{
    auto* item = new QListWidgetItem(QString::fromStdString(text::formatMessage(message, kHostId)));
    item->setData(kSentAtRole, static_cast<qlonglong>(message.sentAtMs));
    item->setData(kSeqRole, static_cast<qulonglong>(message.seq));
    if (message.isPrivate()) {
        QFont font = item->font();
        font.setItalic(true);
        item->setFont(font);
    }

    // Messages are shown in the order they were sent, even if they reached the host out of order
    int row = chat_->count();
    while (row > 0 && itemSentAfter(chat_->item(row - 1), message)) {
        --row;
    }
    chat_->insertItem(row, item);
    if (row == chat_->count() - 1) {
        chat_->scrollToBottom();
    }
}

void HostWindow::showParticipants(const std::vector<Participant>& participants)
{
    const int selected = recipient_->currentData().toInt();
    participants_->clear();
    recipient_->clear();
    recipient_->addItem(QStringLiteral("Всем"), kEveryone);

    for (const Participant& participant : participants) {
        const QString label =
            QStringLiteral("%1 · %2").arg(participant.id).arg(QString::fromStdString(participant.name));
        if (participant.id == kHostId) {
            participants_->addItem(label + QStringLiteral(" (вы)"));
            continue;
        }
        participants_->addItem(label);
        recipient_->addItem(label, participant.id);
    }
    const int index = recipient_->findData(selected);
    recipient_->setCurrentIndex(index >= 0 ? index : 0);
}

}
