#include "goat.h"

#include "conn/conn.h"
#include "game/game.h"
#include "game/protocol.h"

#include <cstdlib>
#include <exception>

#include <syslog.h>
#include <unistd.h>

namespace {

	bool playWithWolf(Conn& conn){
		Message message{MessageType::Hello, getpid()};
		bool connected = conn.Write(&message, sizeof message);
		if (connected) {
			syslog(LOG_INFO, "connected to the wolf");
		}

		bool alive = true;
		bool finished = false;
		while (connected && !finished && conn.Read(&message, sizeof message)) {
			if (message.type == MessageType::Stop) {
				finished = true;
			} else if (message.type == MessageType::Round) {
				int maxNumber = game::DEAD_MAX_NUMBER;
				if (alive) {
					maxNumber = game::MAX_NUMBER;
				}
				Message move{MessageType::Move, game::roll(maxNumber)};
				connected = conn.Write(&move, sizeof move) && conn.Read(&message, sizeof message)
					&& message.type == MessageType::Status;
				if (connected) {
					alive = message.value != 0;
				}
			} else {
				connected = false;
			}
		}

		if (finished && alive) {
			syslog(LOG_INFO, "game over, the goat is alive");
		} else if (finished) {
			syslog(LOG_INFO, "game over, the goat is dead");
		}
		return finished;
	}

}

int runGoat(int connId){
	openlog("goat", LOG_PID | LOG_PERROR, LOG_USER);
	bool finished = false;
	try {
		Conn conn(connId, false);
		finished = playWithWolf(conn);
	} catch (const std::exception& e) {
		syslog(LOG_ERR, "%s", e.what());
	}

	int code = EXIT_FAILURE;
	if (finished) {
		code = EXIT_SUCCESS;
	} else {
		syslog(LOG_ERR, "connection with the wolf is broken, stopping");
	}
	return code;
}
