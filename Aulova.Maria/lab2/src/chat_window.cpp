#include "chat_window.h"

#include <sys/wait.h>

#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSocketNotifier>
#include <QVBoxLayout>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <string_view>

namespace {

constexpr int kChildPollMs = 200;
constexpr int kClientExitDelayMs = 3000;

const QColor kGreen(0x2e, 0x7d, 0x32);
const QColor kRed(0xc6, 0x28, 0x28);
const QColor kGray(0x75, 0x75, 0x75);
const QColor kPurple(0x8e, 0x24, 0xaa);

QString errnoText()
{
    return QString::fromLocal8Bit(std::strerror(errno));
}

}

ChatWindow::ChatWindow(Connection& connection, Party self, pid_t childPid, int idleLimitMs, QWidget* parent)
    : QWidget(parent), m_connection(connection), m_self(self), m_childPid(childPid), m_idleLimitMs(idleLimitMs)
{
    const bool isHost = self == Party::Host;
    setWindowTitle(isHost ? tr("Чат — Хост") : tr("Чат — Клиент"));
    resize(460, 420);
    move(isHost ? 80 : 600, 80);

    m_status = new QLabel(this);
    m_status->setObjectName("status");
    m_status->setWordWrap(true);

    m_history = new QListWidget(this);
    m_history->setObjectName("history");
    m_history->setWordWrap(true);
    m_history->setTextElideMode(Qt::ElideNone);
    m_history->setSelectionMode(QAbstractItemView::NoSelection);
    m_history->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_recipient = new QComboBox(this);
    m_recipient->setObjectName("recipient");
    m_recipient->addItem(tr("Всем"), static_cast<int>(Party::Everyone));
    m_recipient->addItem(isHost ? tr("Лично клиенту") : tr("Лично хосту"), static_cast<int>(peerOf(self)));

    m_input = new QLineEdit(this);
    m_input->setObjectName("input");
    m_input->setPlaceholderText(tr("Введите сообщение"));

    m_send = new QPushButton(tr("Отправить"), this);
    m_send->setObjectName("send");

    auto* inputRow = new QHBoxLayout;
    inputRow->addWidget(m_recipient);
    inputRow->addWidget(m_input, 1);
    inputRow->addWidget(m_send);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_status);
    layout->addWidget(m_history, 1);
    layout->addLayout(inputRow);

    connect(m_send, &QPushButton::clicked, this, &ChatWindow::sendMessage);
    connect(m_input, &QLineEdit::returnPressed, this, &ChatWindow::sendMessage);

    m_notifier = new QSocketNotifier(connection.readFd(), QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &ChatWindow::receiveMessages);

    setStatus(tr("Подключено"), kGreen);
    if (isHost) {
        notice(tr("Хост запущен, клиентский процесс (PID %1) подключён. Клиент, не приславший сообщений более %2 с, "
                  "будет отключён.")
                   .arg(childPid)
                   .arg(idleLimitMs / 1000.0),
               kGray);

        m_idleTimer.setSingleShot(true);
        m_idleTimer.setInterval(idleLimitMs);
        connect(&m_idleTimer, &QTimer::timeout, this, &ChatWindow::idleTimeout);
        m_idleTimer.start();

        m_childTimer.setInterval(kChildPollMs);
        connect(&m_childTimer, &QTimer::timeout, this, &ChatWindow::checkChild);
        m_childTimer.start();
    } else {
        notice(tr("Клиент запущен и подключён к хосту."), kGray);
    }
    m_input->setFocus();
}

QString ChatWindow::nameOf(Party party) const
{
    switch (party) {
    case Party::Host:
        return tr("Хост");
    case Party::Client:
        return tr("Клиент");
    case Party::Everyone:
        break;
    }
    return tr("Все");
}

QString ChatWindow::timeText(qint64 micros) const
{
    return QDateTime::fromMSecsSinceEpoch(micros / 1000).toString(QStringLiteral("HH:mm:ss"));
}

void ChatWindow::addEntry(qint64 micros, const QString& text, const QColor& color)
{
    auto* item = new QListWidgetItem(QStringLiteral("[%1] %2").arg(timeText(micros), text));
    item->setData(Qt::UserRole, micros);
    if (color.isValid())
        item->setForeground(color);

    int row = m_history->count();
    while (row > 0 && m_history->item(row - 1)->data(Qt::UserRole).toLongLong() > micros)
        --row;
    m_history->insertItem(row, item);
    m_history->scrollToBottom();
}

void ChatWindow::notice(const QString& text, const QColor& color)
{
    addEntry(nowMicros(), text, color);
}

void ChatWindow::showMessage(const WireMessage& message)
{
    const bool isPrivate = message.recipient != Party::Everyone;
    QString line = nameOf(message.sender);
    if (isPrivate)
        line += tr(" → %1 (лично)").arg(nameOf(message.recipient));
    line += QStringLiteral(": ") + QString::fromUtf8(message.text, static_cast<qsizetype>(message.length));
    addEntry(message.sentAtUs, line, isPrivate ? kPurple : QColor());
}

void ChatWindow::setStatus(const QString& text, const QColor& color)
{
    m_status->setText(QStringLiteral("● ") + text);
    m_status->setStyleSheet(QStringLiteral("color: %1; font-weight: bold;").arg(color.name()));
}

void ChatWindow::endConnection(const QString& reason)
{
    if (!m_connected)
        return;
    m_connected = false;
    m_notifier->setEnabled(false);
    m_idleTimer.stop();
    m_input->setEnabled(false);
    m_recipient->setEnabled(false);
    m_send->setEnabled(false);
    setStatus(tr("Нет соединения — %1").arg(reason), kRed);
    notice(tr("Соединение завершено: %1.").arg(reason), kRed);
    if (m_self == Party::Client)
        QTimer::singleShot(kClientExitDelayMs, this, [this] { close(); });
}

void ChatWindow::sendMessage()
{
    const QString text = m_input->text();
    if (!m_connected || text.trimmed().isEmpty())
        return;

    const QByteArray utf8 = text.toUtf8();
    const Party recipient = static_cast<Party>(m_recipient->currentData().toInt());
    const auto message = makeText(m_self, recipient, std::string_view(utf8.constData(), static_cast<size_t>(utf8.size())));
    if (!message) {
        notice(tr("Сообщение не отправлено: слишком длинное (%1 байт, максимум %2).").arg(utf8.size()).arg(kMaxTextBytes),
               kRed);
        return;
    }

    switch (m_connection.send(*message)) {
    case SendResult::Ok:
        showMessage(*message);
        m_input->clear();
        break;
    case SendResult::Busy:
        notice(tr("Канал временно занят, сообщение не отправлено. Повторите отправку."), kRed);
        break;
    case SendResult::Closed:
        endConnection(tr("соединение закрыто собеседником"));
        break;
    case SendResult::Error:
        endConnection(QString::fromStdString(m_connection.error()));
        break;
    }
}

void ChatWindow::receiveMessages()
{
    WireMessage message;
    while (m_connected) {
        switch (m_connection.receive(message)) {
        case RecvResult::Message:
            handleIncoming(message);
            break;
        case RecvResult::Empty:
            return;
        case RecvResult::Closed:
            endConnection(tr("соединение закрыто собеседником"));
            return;
        case RecvResult::Error:
            endConnection(QString::fromStdString(m_connection.error()));
            return;
        }
    }
}

void ChatWindow::handleIncoming(const WireMessage& message)
{
    if (message.kind == Kind::Bye) {
        endConnection(tr("%1 завершил работу").arg(nameOf(message.sender).toLower()));
        if (m_self == Party::Client)
            close();
        return;
    }
    if (m_self == Party::Host)
        m_idleTimer.start();
    if (::isVisibleTo(message, m_self))
        showMessage(message);
}

void ChatWindow::idleTimeout()
{
    if (!m_connected)
        return;
    if (m_childPid > 0)
        kill(m_childPid, SIGKILL);
    endConnection(tr("тайм-аут: от клиента нет сообщений более %1 с, процесс завершён (SIGKILL)")
                      .arg(m_idleLimitMs / 1000.0));
}

void ChatWindow::checkChild()
{
    int status = 0;
    const pid_t result = waitpid(m_childPid, &status, WNOHANG);
    if (result == 0 || (result < 0 && errno == EINTR))
        return;

    const pid_t pid = m_childPid;
    m_childPid = -1;
    m_childTimer.stop();

    QString how;
    if (result < 0)
        how = tr("статус недоступен (%1)").arg(errnoText());
    else if (WIFEXITED(status))
        how = tr("код завершения %1").arg(WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        how = tr("убит сигналом %1 (%2)").arg(WTERMSIG(status)).arg(QString::fromLocal8Bit(strsignal(WTERMSIG(status))));
    notice(tr("Клиентский процесс (PID %1) завершён: %2.").arg(pid).arg(how), kGray);
    endConnection(tr("клиентский процесс завершился"));
}

void ChatWindow::closeEvent(QCloseEvent* event)
{
    if (m_connected) {
        m_connection.send(makeBye(m_self));
        m_connected = false;
        m_notifier->setEnabled(false);
        m_idleTimer.stop();
    }
    event->accept();
}
