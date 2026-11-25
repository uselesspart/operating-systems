#include "frontend.hpp"
#include "host.hpp"
#include "client.hpp"
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMetaObject>
#include <QPushButton>
#include <QShortcut>
#include <QStatusBar>
#include <QTextEdit>
#include <QVBoxLayout>
#include <thread>
#include <iostream>
#include <unistd.h>

namespace application {
namespace {
class ChatWindow final : public QMainWindow {
    QTextEdit* history_;
    QComboBox* recipients_;
    QLineEdit* input_;
    QPushButton* send_;
    std::unique_ptr<Server> server_;
    std::unique_ptr<Client> client_;
    std::jthread bootstrap_;
    bool failed_ = false;

    void append(const QString& text) {
        history_->append(text.toHtmlEscaped());
    }
    void event(const Event& value) {
        const auto& packet = value.packet;
        const auto name = QString::fromStdString(packet.name);
        const auto detail = QString::fromStdString(value.text);
        switch (value.kind) {
        case EventKind::Message: {
            const auto target = packet.recipient ? QString::number(packet.recipient) : QStringLiteral("всем");
            const auto time = QDateTime::fromMSecsSinceEpoch(packet.time_ms).toString("HH:mm:ss.zzz");
            append(QString("%1  #%2  %3 → %4: %5").arg(time).arg(packet.sequence)
                .arg(name, target, QString::fromStdString(packet.text)));
            break;
        }
        case EventKind::Joined:
            if (recipients_->findData(packet.sender) < 0) {
                recipients_->addItem(QString("%1 (PID %2)").arg(name).arg(packet.sender), packet.sender);
            }
            append(QString("[Подключение] %1, PID %2").arg(name).arg(packet.sender));
            break;
        case EventKind::Left: {
            const int index = recipients_->findData(packet.sender);
            if (index >= 0) { recipients_->removeItem(index); }
            append(QString("[Отключение] %1: %2").arg(name, detail));
            break;
        }
        case EventKind::Error:
            failed_ = true;
            append("[Ошибка] " + detail);
            statusBar()->showMessage("Ошибка: " + detail);
            break;
        case EventKind::Status:
            append("[Статус] " + detail);
            statusBar()->showMessage(detail);
            if (detail.startsWith("READY")) { send_->setEnabled(true); input_->setEnabled(true); }
            break;
        case EventKind::Stopped:
            append("[Завершение] " + detail);
            statusBar()->showMessage("Соединение завершено");
            send_->setEnabled(false);
            input_->setEnabled(false);
            QMetaObject::invokeMethod(this, [this] { close(); }, Qt::QueuedConnection);
            break;
        }
    }
    void send_message() {
        try {
            const auto text = input_->text().toStdString();
            const int recipient = recipients_->currentData().toInt();
            if (server_) { server_->post_message(text, recipient); }
            else { client_->post_message(text, recipient); }
            input_->clear();
        } catch (const std::exception& error) { append("[Ошибка] " + QString::fromUtf8(error.what())); }
    }
public:
    bool failed() const noexcept { return failed_; }
    ChatWindow(const Options& options, bool host) {
        setWindowTitle(QString("PolyNet · %1 · %2 · PID %3")
            .arg(host ? "Хост" : "Клиент", TYPE_CODE).arg(getpid()));
        resize(820, 540);
        auto* central = new QWidget(this);
        auto* layout = new QVBoxLayout(central);
        layout->addWidget(new QLabel(QString("%1 · %2").arg(QString::fromStdString(options.name),
            host ? "Хост чата" : QString("Хост: PID %1").arg(options.host_pid)), central));
        history_ = new QTextEdit(central);
        history_->setObjectName("history");
        history_->setReadOnly(true);
        history_->document()->setMaximumBlockCount(5000);
        layout->addWidget(history_, 1);
        auto* row = new QHBoxLayout;
        recipients_ = new QComboBox(central);
        recipients_->setObjectName("recipients");
        recipients_->addItem("Всем участникам", 0);
        row->addWidget(recipients_);
        input_ = new QLineEdit(central);
        input_->setObjectName("messageInput");
        input_->setPlaceholderText("Введите сообщение");
        input_->setMaxLength(2048);
        input_->setEnabled(false);
        row->addWidget(input_, 1);
        send_ = new QPushButton("Отправить", central);
        send_->setObjectName("sendButton");
        send_->setEnabled(false);
        row->addWidget(send_);
        layout->addLayout(row);
        layout->addWidget(new QLabel("Клиент завершается хостом, если не отправляет сообщения более 60 секунд.", central));
        setCentralWidget(central);
        statusBar()->showMessage("Запуск соединения…");
        connect(send_, &QPushButton::clicked, this, [this] { send_message(); });
        connect(input_, &QLineEdit::returnPressed, this, [this] { send_message(); });
        auto* quit = new QShortcut(QKeySequence::Quit, this);
        connect(quit, &QShortcut::activated, this, &QWidget::close);
        auto focus_shortcut = [this](const char* keys, QWidget* widget) {
            auto* shortcut = new QShortcut(QKeySequence(keys), this);
            connect(shortcut, &QShortcut::activated, widget, [widget] { widget->setFocus(); });
        };
        focus_shortcut("Alt+H", history_);
        focus_shortcut("Alt+R", recipients_);
        focus_shortcut("Alt+M", input_);
        // The context object cancels queued callbacks when the window is destroyed.
        // All producers are joined before destruction; widgets are only touched here.
        auto sink = [this](const Event& value) {
            if (value.kind == EventKind::Error) { std::cerr << "[error] " << value.text << std::endl; }
            QMetaObject::invokeMethod(this, [this, value] { event(value); }, Qt::QueuedConnection);
        };
        if (host) { server_ = std::make_unique<Server>(ServerConfig{options.name}, sink); }
        else { client_ = std::make_unique<Client>(ClientConfig{options.host_pid, options.name}, sink); }
        bootstrap_ = std::jthread([this, sink] {
            try { if (server_) { server_->start(); } else { client_->start(); } }
            catch (const std::exception& error) {
                notify(sink, EventKind::Error, error.what());
                notify(sink, EventKind::Stopped, "Startup failed");
            }
        });
    }
    ~ChatWindow() override {
        // A pending handshake is bounded by five seconds per operation.
        if (bootstrap_.joinable()) { bootstrap_.join(); }
        if (server_) { server_->request_stop(); server_->join(); }
        if (client_) { client_->request_stop(); client_->join(); }
    }
};
} // namespace
int run_gui(int argc, char** argv, const Options& options, bool host) {
    QApplication app(argc, argv);
    ChatWindow window(options, host);
    window.show();
    const int result = app.exec();
    return window.failed() ? 1 : result;
}
} // namespace application
