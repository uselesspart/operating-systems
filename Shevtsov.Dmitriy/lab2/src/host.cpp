#include "conn/conn.h"
#include "goat/goat.h"
#include "window/window.h"

#include <csignal>
#include <cstdlib>
#include <exception>

#include <QApplication>

#include <syslog.h>
#include <unistd.h>

int main(int argc, char* argv[]){
	openlog("wolf", LOG_PID | LOG_PERROR, LOG_USER);
	signal(SIGPIPE, SIG_IGN);

	const int connId = getpid();
	bool prepared = false;
	try {
		Conn::Prepare();
		prepared = true;
	} catch (const std::exception& e) {
		syslog(LOG_ERR, "cannot prepare the connection: %s", e.what());
	}

	pid_t goatPid = -1;
	if (prepared) {
		goatPid = fork();
	}
	if (prepared && goatPid < 0) {
		syslog(LOG_ERR, "fork: %m");
	}
	if (goatPid == 0) {
		_exit(runGoat(connId));
	}

	int code = EXIT_FAILURE;
	if (goatPid > 0) {
		QApplication app(argc, argv);
		Window window(connId, goatPid);
		window.show();
		code = app.exec();
	}
	return code;
}
