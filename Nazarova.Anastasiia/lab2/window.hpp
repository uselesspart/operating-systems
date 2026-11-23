#pragma once

#include <QJsonObject>
#include <QWidget>
#include <functional>
#include <sys/types.h>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QListWidget;

class ChatWindow : public QWidget {
public:
    ChatWindow(pid_t host, pid_t self);
    void Handle(const QJsonObject& event);
    void SetSender(std::function<void(pid_t, const QString&)> sender);
private:
    QString participant(pid_t pid) const;
    void send();
    pid_t host_;
    pid_t self_;
    QLabel* status_;
    QComboBox* recipient_;
    QListWidget* messages_;
    QLineEdit* text_;
    QPushButton* send_;
    std::function<void(pid_t, const QString&)> sender_;
};

int runApplication(bool host, int argc, char** argv);
