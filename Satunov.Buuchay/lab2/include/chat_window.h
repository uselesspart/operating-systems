#pragma once

#include "chat_endpoint.h"
#include "process_manager.h"

#include <QWidget>
#include <QString>

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>

class QLabel;
class QPlainTextEdit;
class QLineEdit;
class QComboBox;
class QPushButton;

class ChatWindow final : public QWidget {
public:
    ChatWindow(
        chat::ChatEndpoint& endpoint,
        ChildProcess& child,
        std::atomic<bool>& connected,
        std::mutex& error_mutex,
        std::string& error_text,
        QWidget* parent = nullptr
    );

private:
    void send_message();
    void refresh();
    void refresh_history();
    void set_status(const QString& message);
    void disconnect_client(const QString& reason);

    chat::ChatEndpoint& endpoint_;
    ChildProcess& child_;
    std::atomic<bool>& connected_;
    std::mutex& error_mutex_;
    std::string& error_text_;

    QLabel* status_label_{nullptr};
    QPlainTextEdit* history_{nullptr};
    QPlainTextEdit* events_{nullptr};
    QLineEdit* input_{nullptr};
    QComboBox* message_type_{nullptr};
    QPushButton* send_button_{nullptr};

    std::size_t rendered_count_{0};
    bool disconnected_shown_{false};
};