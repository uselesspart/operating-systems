#pragma once

#include "game/game.h"

#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <sys/types.h>

class Conn;

struct RoundReport {
	int round = 0;
	int wolf = 0;
	bool wolfRandom = false;
	int goat = 0;
	bool goatWasAlive = false;
	game::RoundResult result;
};

class Wolf {
public:
	struct Callbacks {
		std::function<void(const std::string&)> status;
		std::function<void(int round)> turn;
		std::function<void(const RoundReport&)> round;
		std::function<void()> finished;
	};

	Wolf(int connId, pid_t goatPid, Callbacks callbacks);
	~Wolf();

	void submitNumber(int number);

private:
	void run();
	void play(Conn& conn);
	void playRounds(Conn& conn);
	int waitWolfNumber(bool& random);
	bool isStopping();
	void reapGoat();

	int connId;
	pid_t goatPid;
	Callbacks callbacks;

	std::mutex mutex;
	std::condition_variable wakeup;
	std::optional<int> userNumber;
	bool waiting = false;
	bool stopping = false;

	std::thread worker;
};
