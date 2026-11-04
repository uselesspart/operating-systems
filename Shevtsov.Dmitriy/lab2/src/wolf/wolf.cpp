#include "wolf.h"

#include "conn/conn.h"
#include "game/protocol.h"

#include <chrono>
#include <csignal>
#include <exception>
#include <memory>

#include <sys/wait.h>
#include <syslog.h>

namespace {

	using namespace std::chrono;

	const auto WOLF_INPUT_TIMEOUT = seconds(3);
	const auto REAP_INTERVAL = milliseconds(50);
	const int GOATS = 1;

	bool exchange(Conn& conn, Message& message, MessageType expected){
		return conn.Write(&message, sizeof message)
			&& conn.Read(&message, sizeof message)
			&& message.type == expected;
	}

}

Wolf::Wolf(int connId, pid_t goatPid, Callbacks callbacks)
	: connId(connId), goatPid(goatPid), callbacks(std::move(callbacks)), worker(&Wolf::run, this){}

Wolf::~Wolf(){
	{
		std::lock_guard lock(mutex);
		stopping = true;
	}
	wakeup.notify_all();
	worker.join();
}

void Wolf::submitNumber(int number){
	std::lock_guard lock(mutex);
	if (waiting) {
		userNumber = number;
		wakeup.notify_all();
	}
}

void Wolf::run(){
	callbacks.status("Козлёнок запущен (pid " + std::to_string(goatPid) + "), ждём подключения");

	std::unique_ptr<Conn> conn;
	try {
		conn = std::make_unique<Conn>(connId, true);
		play(*conn);
	} catch (const std::exception& e) {
		syslog(LOG_ERR, "%s", e.what());
		callbacks.status(std::string("Ошибка соединения: ") + e.what());
	}

	reapGoat();
	callbacks.finished();
}

void Wolf::play(Conn& conn){
	Message message{};
	if (conn.Read(&message, sizeof message) && message.type == MessageType::Hello) {
		syslog(LOG_INFO, "goat %d connected", message.value);
		callbacks.status("Козлёнок присоединился (pid " + std::to_string(message.value) + ")");
		playRounds(conn);
	} else {
		callbacks.status("Козлёнок не подключился");
	}
}

void Wolf::playRounds(Conn& conn){
	game::Game game(GOATS);
	bool connected = true;
	for (int round = 1; connected && !game.isOver() && !isStopping(); round++) {
		RoundReport report;
		report.round = round;
		report.goatWasAlive = game.isAlive(0);

		Message message{MessageType::Round, round};
		connected = exchange(conn, message, MessageType::Move);
		if (connected) {
			report.goat = message.value;
			callbacks.turn(round);
			report.wolf = waitWolfNumber(report.wolfRandom);
			report.result = game.playRound(report.wolf, {report.goat});
			callbacks.round(report);

			message = {MessageType::Status, game.isAlive(0)};
			connected = conn.Write(&message, sizeof message);
		}
	}

	if (!connected) {
		callbacks.status("Связь с козлёнком потеряна");
	} else {
		Message message{MessageType::Stop, 0};
		conn.Write(&message, sizeof message);
		if (game.isOver()) {
			callbacks.status("Игра окончена: козлята мертвы два хода подряд");
		} else {
			callbacks.status("Игра прервана");
		}
	}
}

int Wolf::waitWolfNumber(bool& random){
	std::unique_lock lock(mutex);
	userNumber.reset();
	waiting = true;
	wakeup.wait_for(lock, WOLF_INPUT_TIMEOUT, [this]{ return userNumber || stopping; });
	waiting = false;

	random = !userNumber;
	int number = 0;
	if (random) {
		number = game::roll(game::MAX_NUMBER);
	} else {
		number = *userNumber;
	}
	return number;
}

bool Wolf::isStopping(){
	std::lock_guard lock(mutex);
	return stopping;
}

void Wolf::reapGoat(){
	int status = 0;
	pid_t result;
	auto deadline = steady_clock::now() + seconds(CONN_TIMEOUT_SEC);
	while ((result = waitpid(goatPid, &status, WNOHANG)) == 0 && steady_clock::now() < deadline) {
		std::this_thread::sleep_for(REAP_INTERVAL);
	}

	if (result == 0) {
		syslog(LOG_WARNING, "goat %d did not exit in time, killing it", goatPid);
		kill(goatPid, SIGKILL);
		result = waitpid(goatPid, &status, 0);
	}
	if (result < 0) {
		syslog(LOG_ERR, "waitpid: %m");
	} else if (WIFEXITED(status)) {
		callbacks.status("Козлёнок завершился (код " + std::to_string(WEXITSTATUS(status)) + ")");
	} else {
		callbacks.status("Козлёнок убит сигналом " + std::to_string(WTERMSIG(status)));
	}
}
