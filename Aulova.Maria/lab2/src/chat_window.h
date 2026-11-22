#pragma once

#include <sys/types.h>

#include <QColor>
#include <QString>
#include <QTimer>
#include <QWidget>

#include "connection.h"
#include "message.h"

class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSocketNotifier;

constexpr int kIdleLimitMs = 60000;

class ChatWindow : public QWidget {
    Q_OBJECT

public:
    ChatWindow(Connection& connection, Party self, pid_t childPid, int idleLimitMs = kIdleLimitMs,
               QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void sendMessage();
    void receiveMessages();
    void idleTimeout();
    void checkChild();

private:
    QString nameOf(Party party) const;
    QString timeText(qint64 micros) const;
    void addEntry(qint64 micros, const QString& text, const QColor& color = QColor());
    void notice(const QString& text, const QColor& color);
    void showMessage(const WireMessage& message);
    void handleIncoming(const WireMessage& message);
    void setStatus(const QString& text, const QColor& color);
    void endConnection(const QString& reason);

    Connection& m_connection;
    Party m_self;
    pid_t m_childPid;
    int m_idleLimitMs;
    bool m_connected = true;

    QLabel* m_status = nullptr;
    QListWidget* m_history = nullptr;
    QComboBox* m_recipient = nullptr;
    QLineEdit* m_input = nullptr;
    QPushButton* m_send = nullptr;
    QSocketNotifier* m_notifier = nullptr;
    QTimer m_idleTimer;
    QTimer m_childTimer;
};
