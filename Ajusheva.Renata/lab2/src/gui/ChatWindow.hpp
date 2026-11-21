#pragma once

#include "host/HostEvents.hpp"

#include <QMainWindow>

#include <functional>
#include <string>
#include <utility>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

namespace chatlab {

class ChatWindow : public QMainWindow, public HostEvents {
  public:
    using SendHandler = std::function<void(int to, const std::string& text)>;

    ChatWindow(const QString& kind, qint64 pid, QWidget* parent = nullptr);

    void setSendHandler(SendHandler handler) { sendHandler_ = std::move(handler); }

    void onStatus(const std::string& text) override;
    void onRoster(const std::vector<Member>& roster) override;
    void onPost(const Post& post) override;
    void onQuitRequest() override;

  private:
    void submitInput();
    void appendStatus(const QString& text);
    void appendPost(const Post& post);
    void applyRoster(const std::vector<Member>& roster);
    void refreshCounter();
    void later(std::function<void()> action);

    SendHandler sendHandler_;
    QString kind_;
    qint64 pid_;
    int clients_ = 0;
    QPlainTextEdit* feed_;
    QPlainTextEdit* journal_;
    QListWidget* members_;
    QComboBox* target_;
    QLineEdit* input_;
    QPushButton* send_;
    QLabel* counter_;
};

}
