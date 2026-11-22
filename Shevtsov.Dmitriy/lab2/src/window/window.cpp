#include "window.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTime>
#include <QVBoxLayout>

namespace {

	template<typename F>
	void post(QObject* target, F function){
		QMetaObject::invokeMethod(target, std::move(function), Qt::QueuedConnection);
	}

	QString goatOutcome(const RoundReport& report){
		QString outcome = "остался мёртвым";
		if (report.goatWasAlive && report.result.hidden) {
			outcome = "спрятался";
		} else if (report.goatWasAlive) {
			outcome = "попался";
		} else if (report.result.resurrected) {
			outcome = "воскрес";
		}
		return outcome;
	}

}

Window::Window(int connId, pid_t goatPid){
	setWindowTitle("Волк и семеро козлят");
	resize(640, 480);

	status = new QLabel;
	prompt = new QLabel("Ждём козлёнка...");
	number = new QSpinBox;
	number->setRange(1, game::MAX_NUMBER);
	throwButton = new QPushButton("Бросить");
	throwButton->setEnabled(false);
	stats = new QLabel("Спрятались: 0   Попались: 0   Мертвы: 0");
	history = new QPlainTextEdit;
	history->setReadOnly(true);

	auto* input = new QHBoxLayout;
	input->addWidget(prompt, 1);
	input->addWidget(number);
	input->addWidget(throwButton);

	auto* layout = new QVBoxLayout(this);
	layout->addWidget(status);
	layout->addLayout(input);
	layout->addWidget(stats);
	layout->addWidget(history, 1);

	connect(throwButton, &QPushButton::clicked, this, [this]{
		throwButton->setEnabled(false);
		wolf->submitNumber(number->value());
	});

	showStatus("Волк включился");

	Wolf::Callbacks callbacks;
	callbacks.status = [this](const std::string& text){
		post(this, [this, text]{ showStatus(QString::fromStdString(text)); });
	};
	callbacks.turn = [this](int round){
		post(this, [this, round]{ showTurn(round); });
	};
	callbacks.round = [this](const RoundReport& report){
		post(this, [this, report]{ showRound(report); });
	};
	callbacks.finished = [this]{
		post(this, [this]{ showFinished(); });
	};
	wolf = std::make_unique<Wolf>(connId, goatPid, std::move(callbacks));
}

Window::~Window(){
	wolf.reset();
}

void Window::showStatus(const QString& text){
	status->setText(text);
	history->appendPlainText(QTime::currentTime().toString("[hh:mm:ss] ") + text);
}

void Window::showTurn(int round){
	prompt->setText(QString("Ход %1: введите число волка за 3 с, иначе оно выпадет случайно").arg(round));
	throwButton->setEnabled(true);
}

void Window::showRound(const RoundReport& report){
	throwButton->setEnabled(false);
	prompt->setText(QString("Ход %1 сыгран").arg(report.round));

	const auto& result = report.result;
	stats->setText(QString("Спрятались: %1   Попались: %2   Мертвы: %3")
		.arg(result.hidden).arg(result.caught).arg(result.dead));

	QString randomMark;
	if (report.wolfRandom) {
		randomMark = " (случайно)";
	}
	history->appendPlainText(QString("Ход %1: волк %2%3, козлёнок %4 (%5). Спрятались: %6, попались: %7, мертвы: %8")
		.arg(report.round)
		.arg(report.wolf)
		.arg(randomMark)
		.arg(report.goat)
		.arg(goatOutcome(report))
		.arg(result.hidden)
		.arg(result.caught)
		.arg(result.dead));
}

void Window::showFinished(){
	throwButton->setEnabled(false);
	prompt->setText("Игра завершена");
}
