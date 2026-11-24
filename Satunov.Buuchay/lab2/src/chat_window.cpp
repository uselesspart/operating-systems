#include "chat_window.h"

#include "chat_message.h"

#include <QByteArray>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QStyle>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

#include <csignal>
#include <cstddef>
#include <exception>
#include <string>
#include <utility>

ChatWindow::ChatWindow(
    chat::ChatEndpoint& endpoint,
    ChildProcess& child,
    std::atomic<bool>& connected,
    std::mutex& error_mutex,
    std::string& error_text,
    QWidget* parent
)
    : QWidget(parent),
      endpoint_(endpoint),
      child_(child),
      connected_(connected),
      error_mutex_(error_mutex),
      error_text_(error_text) {

    setObjectName("chatRoot");
    setAttribute(Qt::WA_StyledBackground, true);
    setWindowTitle("Chat");
    setMinimumSize(640, 500);
    resize(900, 660);

    setStyleSheet(R"QSS(
        QWidget#chatRoot {
            background: #0B1020;
            color: #E8EDF8;
            font-family: "Segoe UI", "DejaVu Sans", sans-serif;
            font-size: 13px;
        }
        QLabel {
            color: #E8EDF8;
            background: transparent;
        }
        QLabel#brand {
            color: #F7F9FF;
            font-size: 27px;
            font-weight: 800;
        }
        QLabel#subtitle {
            color: #8996B0;
            font-size: 12px;
        }
        QLabel#sectionTitle {
            color: #9AA8C3;
            font-size: 11px;
            font-weight: 700;
        }
        QLabel#statusBadge {
            background: #12392F;
            color: #69E9BC;
            border: 1px solid #235747;
            border-radius: 13px;
            padding: 9px 13px;
            font-weight: 600;
        }
        QLabel#statusBadge[disconnected="true"] {
            background: #3B202C;
            color: #FF9BA8;
            border: 1px solid #704055;
        }
        QFrame#separator {
            background: #253149;
            border: none;
            max-height: 1px;
        }
        QPlainTextEdit#chatHistory {
            background: #131B2D;
            color: #E8EDF8;
            border: 1px solid #29354D;
            border-radius: 16px;
            padding: 17px 19px;
            selection-background-color: #455E9C;
            font-family: "DejaVu Sans", "Segoe UI", sans-serif;
            font-size: 13px;
        }
        QPlainTextEdit#eventLog {
            background: #101827;
            color: #A6B4D0;
            border: 1px solid #29354D;
            border-radius: 12px;
            padding: 9px 12px;
            selection-background-color: #455E9C;
            font-family: "DejaVu Sans Mono", monospace;
            font-size: 11px;
        }
        QLineEdit#messageInput {
            background: #172238;
            color: #F7F9FF;
            border: 1px solid #36445E;
            border-radius: 12px;
            padding: 0 15px;
            font-size: 13px;
            selection-background-color: #516EC6;
        }
        QLineEdit#messageInput:focus {
            border: 1px solid #809EFF;
            background: #1B2942;
        }
        QLineEdit#messageInput:disabled {
            color: #71819D;
            background: #141B29;
        }
        QComboBox#messageKind {
            background: #172238;
            color: #E8EDF8;
            border: 1px solid #36445E;
            border-radius: 12px;
            padding: 0 12px;
            font-weight: 600;
        }
        QComboBox#messageKind:focus {
            border: 1px solid #809EFF;
        }
        QComboBox#messageKind:disabled {
            color: #71819D;
            background: #141B29;
        }
        QComboBox#messageKind::drop-down {
            border: 0;
            width: 25px;
        }
        QComboBox QAbstractItemView {
            background: #172238;
            color: #E8EDF8;
            border: 1px solid #36445E;
            selection-background-color: #344C85;
            padding: 5px;
        }
        QPushButton#sendButton {
            color: #FFFFFF;
            background: #597CEB;
            border: 0;
            border-radius: 12px;
            padding: 0 17px;
            font-weight: 700;
        }
        QPushButton#sendButton:hover {
            background: #7192FA;
        }
        QPushButton#sendButton:pressed {
            background: #3F60CA;
        }
        QPushButton#sendButton:disabled {
            background: #303B52;
            color: #8794AD;
        }
        QLabel#footerHint, QLabel#byteCounter {
            color: #8290A9;
            font-size: 11px;
        }
        QScrollBar:vertical {
            background: transparent;
            width: 10px;
            margin: 6px 2px;
        }
        QScrollBar::handle:vertical {
            background: #42516D;
            border-radius: 4px;
            min-height: 25px;
        }
        QScrollBar::handle:vertical:hover {
            background: #6279A7;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: none;
        }
    )QSS");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(25, 23, 25, 20);
    root->setSpacing(13);

    auto* top = new QHBoxLayout();
    top->setSpacing(16);

    auto* brandColumn = new QVBoxLayout();
    brandColumn->setSpacing(4);

    auto* brand = new QLabel("Чат", this);
    brand->setObjectName("brand");
    brandColumn->addWidget(brand);

    auto* subtitle = new QLabel(
        "Локальный чат между процессами  ·  Вариант 13", this
    );
    subtitle->setObjectName("subtitle");
    brandColumn->addWidget(subtitle);

    top->addLayout(brandColumn, 1);

    status_label_ = new QLabel(this);
    status_label_->setObjectName("statusBadge");
    status_label_->setProperty("disconnected", false);
    status_label_->setWordWrap(true);
    status_label_->setMaximumWidth(320);
    top->addWidget(status_label_, 0, Qt::AlignRight | Qt::AlignVCenter);
    root->addLayout(top);

    auto* separator = new QFrame(this);
    separator->setObjectName("separator");
    separator->setFrameShape(QFrame::HLine);
    separator->setFixedHeight(1);
    root->addWidget(separator);

    auto* chatTitle = new QLabel("ПЕРЕПИСКА", this);
    chatTitle->setObjectName("sectionTitle");
    root->addWidget(chatTitle);

    history_ = new QPlainTextEdit(this);
    history_->setObjectName("chatHistory");
    history_->setReadOnly(true);
    history_->setPlaceholderText("Здесь появятся ваши сообщения...");
    history_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    root->addWidget(history_, 1);

    auto* composerTitle = new QLabel("НОВОЕ СООБЩЕНИЕ", this);
    composerTitle->setObjectName("sectionTitle");
    root->addWidget(composerTitle);

    auto* controls = new QHBoxLayout();
    controls->setSpacing(9);

    message_type_ = new QComboBox(this);
    message_type_->setObjectName("messageKind");
    message_type_->addItem("Общее");
    message_type_->addItem("Личное");
    message_type_->setMinimumWidth(140);
    message_type_->setFixedHeight(46);
    controls->addWidget(message_type_);

    input_ = new QLineEdit(this);
    input_->setObjectName("messageInput");
    input_->setPlaceholderText("Напишите сообщение...");
    input_->setMinimumHeight(46);
    controls->addWidget(input_, 1);

    send_button_ = new QPushButton("Отправить  →", this);
    send_button_->setObjectName("sendButton");
    send_button_->setMinimumWidth(138);
    send_button_->setFixedHeight(46);
    controls->addWidget(send_button_);
    root->addLayout(controls);

    auto* help = new QHBoxLayout();
    auto* hint = new QLabel("Enter — отправить сообщение", this);
    hint->setObjectName("footerHint");
    help->addWidget(hint);
    help->addStretch();

    auto* byteCounter = new QLabel("0 / 1024 байт", this);
    byteCounter->setObjectName("byteCounter");
    help->addWidget(byteCounter);
    root->addLayout(help);

    connect(
        input_,
        &QLineEdit::textChanged,
        this,
        [byteCounter](const QString& value) {
            const int count = value.toUtf8().size();
            byteCounter->setText(
                QString("%1 / %2 байт")
                    .arg(count)
                    .arg(static_cast<int>(chat::kMaxTextBytes))
            );
            byteCounter->setStyleSheet(
                count > static_cast<int>(chat::kMaxTextBytes)
                    ? "color: #FF9BA8;"
                    : ""
            );
        }
    );

    auto* eventsTitle = new QLabel("СОБЫТИЯ И СОСТОЯНИЕ", this);
    eventsTitle->setObjectName("sectionTitle");
    root->addWidget(eventsTitle);

    events_ = new QPlainTextEdit(this);
    events_->setObjectName("eventLog");
    events_->setReadOnly(true);
    events_->setFixedHeight(100);
    root->addWidget(events_);

    connect(
        send_button_,
        &QPushButton::clicked,
        this,
        [this] { send_message(); }
    );

    connect(
        input_,
        &QLineEdit::returnPressed,
        this,
        [this] { send_message(); }
    );

    auto* timer = new QTimer(this);
    connect(
        timer,
        &QTimer::timeout,
        this,
        [this] { refresh(); }
    );
    timer->start(200);

    set_status("● Клиент подключён");
    refresh_history();
    input_->setFocus();
}

void ChatWindow::set_status(const QString& message) {
    status_label_->setText(message);

    const QString time =
        QDateTime::currentDateTime().toString("HH:mm:ss");

    events_->appendPlainText(
        QString("[%1] %2").arg(time, message)
    );
}

void ChatWindow::disconnect_client(const QString& reason) {
    connected_.store(false);

    if (disconnected_shown_) {
        return;
    }
    disconnected_shown_ = true;

    if (child_.pid() > 0) {
        try {
            child_.terminate(SIGKILL);
        } catch (const std::exception& error) {
            events_->appendPlainText(
                QString("Ошибка остановки клиента: %1")
                    .arg(QString::fromUtf8(error.what()))
            );
        }
    }

    input_->setEnabled(false);
    message_type_->setEnabled(false);
    send_button_->setEnabled(false);

    status_label_->setProperty("disconnected", true);
    status_label_->style()->unpolish(status_label_);
    status_label_->style()->polish(status_label_);
    status_label_->update();

    set_status(reason);
}

void ChatWindow::send_message() {
    if (!connected_.load()) {
        return;
    }

    const QByteArray text = input_->text().toUtf8();

    if (text.isEmpty()) {
        return;
    }

    if (text.size() > static_cast<int>(chat::kMaxTextBytes)) {
        set_status("Сообщение слишком длинное (максимум 1024 байта).");
        return;
    }

    const auto kind =
        message_type_->currentIndex() == 0
            ? chat::MessageKind::Broadcast
            : chat::MessageKind::Private;

    try {
        const std::string message(
            text.constData(),
            static_cast<std::size_t>(text.size())
        );

        if (!endpoint_.send(kind, message)) {
            set_status(
                "Сообщение не отправлено. "
                "Проверьте длину и состояние соединения."
            );
            return;
        }

        input_->clear();
        refresh_history();
        input_->setFocus();

    } catch (const std::exception& error) {
        disconnect_client(
            QString("Ошибка отправки: %1")
                .arg(QString::fromUtf8(error.what()))
        );
    }
}

void ChatWindow::refresh_history() {
    const auto messages = endpoint_.history();

    if (messages.size() == rendered_count_) {
        return;
    }

    rendered_count_ = messages.size();
    history_->clear();

    QTextCursor cursor(history_->document());
    cursor.movePosition(QTextCursor::Start);
    cursor.beginEditBlock();

    QTextCharFormat bodyFormat;
    bodyFormat.setForeground(QColor("#E8EDF8"));

    QTextCharFormat hostFormat;
    hostFormat.setForeground(QColor("#93ADFF"));
    hostFormat.setFontWeight(QFont::DemiBold);

    QTextCharFormat clientFormat;
    clientFormat.setForeground(QColor("#67E6BD"));
    clientFormat.setFontWeight(QFont::DemiBold);

    for (const auto& message : messages) {
        const QString time =
            QDateTime::fromMSecsSinceEpoch(
                static_cast<qint64>(message.sent_at_ms)
            ).toString("HH:mm:ss");

        const bool fromHost =
            message.sender == chat::Participant::Host;

        const QString sender = fromHost ? "ВЫ" : "КЛИЕНТ";
        const QString kind =
            message.kind == chat::MessageKind::Private
                ? "личное"
                : "общее";

        const QString heading =
            QString("●  %1  ·  %2  ·  %3\n")
                .arg(sender, kind, time);

        cursor.insertText(
            heading,
            fromHost ? hostFormat : clientFormat
        );
        cursor.insertText(
            QString::fromStdString(message.text) + "\n\n",
            bodyFormat
        );
    }

    cursor.endEditBlock();

    auto* scrollbar = history_->verticalScrollBar();
    scrollbar->setValue(scrollbar->maximum());
}

void ChatWindow::refresh() {
    refresh_history();

    if (connected_.load() && endpoint_.client_timed_out()) {
        disconnect_client(
            "Клиент завершён через SIGKILL: "
            "60 секунд без сообщений."
        );
    }

    if (child_.pid() > 0) {
        try {
            const auto status = child_.try_wait();

            if (status.has_value()) {
                disconnect_client(
                    "Клиентский процесс завершился."
                );
            }
        } catch (const std::exception& error) {
            disconnect_client(
                QString("Ошибка waitpid: %1")
                    .arg(QString::fromUtf8(error.what()))
            );
        }
    }

    if (!connected_.load() && !disconnected_shown_) {
        std::string error;

        {
            std::lock_guard lock(error_mutex_);
            error = error_text_;
        }

        disconnect_client(
            error.empty()
                ? QString("Соединение закрыто.")
                : QString("Ошибка соединения: %1")
                    .arg(QString::fromStdString(error))
        );
    }
}
