#include "conn/conn.h"
#include "game/game.h"
#include "game/protocol.h"

#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>

#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

#define RUN(test) run(#test, test)

namespace {

	const char* const VERDICT[] = {"FAILED", "PASSED"};

	bool testAliveGoatHidesOnlyNearWolf(){
		game::Game game(1);

		auto hidden = game.playRound(10, {80});
		auto caught = game.playRound(10, {81});

		return hidden.hidden == 1 && caught.caught == 1 && !game.isAlive(0);
	}

	bool testDeadGoatResurrectsNearWolf(){
		game::Game game(2);
		game.playRound(1, {100, 100});

		auto result = game.playRound(50, {60, 61});

		return result.resurrected == 1 && result.dead == 1 && game.isAlive(0) && !game.isAlive(1);
	}

	bool testGameEndsAfterTwoDeadRounds(){
		game::Game game(1);

		game.playRound(1, {100});
		bool overAfterOneRound = game.isOver();
		game.playRound(1, {50});

		return !overAfterOneRound && game.isOver();
	}

	[[noreturn]] void answerAsChild(int id){
		bool ok = false;
		try {
			Conn conn(id, false);
			Message message{};
			ok = conn.Read(&message, sizeof message) && message.type == MessageType::Round;
			message = {MessageType::Move, message.value + 1};
			ok = ok && conn.Write(&message, sizeof message);
		} catch (const std::exception& e) {
			syslog(LOG_ERR, "%s", e.what());
		}

		int code = EXIT_FAILURE;
		if (ok) {
			code = EXIT_SUCCESS;
		}
		_exit(code);
	}

	bool testConnExchangesMessagesWithChild(){
		const int id = getpid();
		Conn::Prepare();

		pid_t child = fork();
		if (child == 0) {
			answerAsChild(id);
		}

		bool ok = false;
		if (child > 0) {
			Conn conn(id, true);
			Message message{MessageType::Round, 41};
			ok = conn.Write(&message, sizeof message) && conn.Read(&message, sizeof message)
				&& message.type == MessageType::Move && message.value == 42;

			int status = 0;
			waitpid(child, &status, 0);
			ok = ok && WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS;
		}
		return ok;
	}

	bool run(const char* name, bool (*test)()){
		std::cout << "[ RUN    ] " << name << std::endl;
		bool passed = false;
		try {
			passed = test();
		} catch (const std::exception& e) {
			std::cout << "  exception: " << e.what() << '\n';
		}
		std::cout << "[ " << VERDICT[passed] << " ] " << name << '\n';
		return passed;
	}

}

int main(){
	openlog("tests", LOG_PID | LOG_PERROR, LOG_USER);
	signal(SIGPIPE, SIG_IGN);

	bool passed = true;
	passed &= RUN(testAliveGoatHidesOnlyNearWolf);
	passed &= RUN(testDeadGoatResurrectsNearWolf);
	passed &= RUN(testGameEndsAfterTwoDeadRounds);
	passed &= RUN(testConnExchangesMessagesWithChild);

	int code = EXIT_FAILURE;
	if (passed) {
		code = EXIT_SUCCESS;
	}
	return code;
}
