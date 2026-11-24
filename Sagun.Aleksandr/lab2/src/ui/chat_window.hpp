#pragma once

#include "chat/message.hpp"
#include <QWidget>

class QComboBox;
class QLineEdit;
class QListWidget;
class QPushButton;

class ChatWindow : public QWidget {
    Q_OBJECT

public:
    ChatWindow(int self, int clientCount, QWidget* parent = nullptr);
    void showMessage(const chat::Message& message);
    void appendLine(const QString& line);
    void clearInput();
    void stopSending();

signals:
    void sendRequested(const chat::Message& message);

private:
    void sendMessage();
    int self_;
    QListWidget* history_;
    QComboBox* recipient_;
    QLineEdit* input_;
    QPushButton* sendButton_;
};
