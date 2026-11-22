#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

class QCloseEvent;

class WolfInputDialog : public QDialog {
    Q_OBJECT
public:
    explicit WolfInputDialog(QWidget* parent = nullptr);
    int value() const { return value_; }

protected:
    void closeEvent(QCloseEvent* event) override;
    void reject() override;

private slots:
    void onSubmit();
    void onTick();

private:
    void finishWith(int v);

    QLineEdit*   edit_;
    QLabel*      countdown_;
    QPushButton* submit_;
    QTimer*      timer_;
    int          remaining_ = 3;
    int          value_     = 0;
};