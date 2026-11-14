#ifndef CHAT_HOST_WINDOW_H
#define CHAT_HOST_WINDOW_H

#include "host/HostInterfaces.h"

#include <QMainWindow>

#include <utility>

class QComboBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

namespace chat {

// Host GUI: the chat, the participants, connection events, and a line to write from the host.
// HostListener callbacks may come from any thread and are forwarded to the GUI thread
class HostWindow : public QMainWindow, public HostListener {
    Q_OBJECT

public:
    HostWindow(const QString& connType, qint64 hostPid, QWidget* parent = nullptr);

    void setController(HostController* controller) { controller_ = controller; }

    void onStatus(const std::string& text) override;
    void onParticipantsChanged(const std::vector<Participant>& participants) override;
    void onMessage(const ChatMessage& message) override;
    void onShutdownRequested() override;

private:
    void sendCurrentMessage();
    void showStatus(const QString& text);
    void showMessage(const ChatMessage& message);
    void showParticipants(const std::vector<Participant>& participants);

    template <typename Action>
    void inGuiThread(Action&& action)
    {
        QMetaObject::invokeMethod(this, std::forward<Action>(action), Qt::QueuedConnection);
    }

    HostController* controller_ = nullptr;
    QListWidget* chat_;
    QListWidget* participants_;
    QPlainTextEdit* events_;
    QComboBox* recipient_;
    QLineEdit* input_;
    QPushButton* sendButton_;
};

}

#endif // CHAT_HOST_WINDOW_H
