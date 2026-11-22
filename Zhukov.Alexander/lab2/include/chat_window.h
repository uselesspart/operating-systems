#pragma once

#include <QWidget>
#include <QLabel>
#include <sys/types.h>
#include "message.h"

class QListWidget;
class QComboBox;
class QLineEdit;
class QPushButton;

class ChatWindow : public QWidget {
    Q_OBJECT

public:
    ChatWindow(const QString& role, const QString& type, pid_t host_pid,
               QWidget* parent = nullptr);
    void set_status(const QString& text);
    void set_connected(bool connected);
    void set_history(const History& messages);
    void clear_input();

signals:
    void send_requested(int recipient, const QString& text);

private:
    void submit();
    QLabel* status;
    QListWidget* history;
    QComboBox* recipient;
    QLineEdit* input;
    QPushButton* send;
};
