#pragma once

#include "wolf/wolf.h"

#include <memory>

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

class Window : public QWidget {
public:
	Window(int connId, pid_t goatPid);
	~Window() override;

private:
	void showStatus(const QString& text);
	void showTurn(int round);
	void showRound(const RoundReport& report);
	void showFinished();

	QLabel* status;
	QLabel* prompt;
	QSpinBox* number;
	QPushButton* throwButton;
	QLabel* stats;
	QPlainTextEdit* history;
	std::unique_ptr<Wolf> wolf;
};
