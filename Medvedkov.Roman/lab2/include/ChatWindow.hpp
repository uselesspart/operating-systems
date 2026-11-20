#pragma once

#include "ChatSession.hpp"
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTextBrowser;
class QTimer;
class QCloseEvent;

namespace chat {
class ChatWindow final : public QWidget {
public:
    ChatWindow(ChatSession& session, const char* transport);
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    void sendInput();
    void showMessage(const Message& message);
    void showStatus(const QString& text);
    void refreshInput();
    ChatSession& session_;
    QTextBrowser* history_;
    QLabel* status_;
    QLabel* counter_;
    QLineEdit* input_;
    QComboBox* recipient_;
    QPushButton* send_;
    QTimer* timer_;
};
}
