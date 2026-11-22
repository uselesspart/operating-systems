#include "Daemon.hpp"

#include <iostream>
#include <cstdlib>
#include <exception>
#include <syslog.h>


int main(int argc, char const *argv[])
{
    
    openlog("my_daemon", LOG_PID | LOG_NDELAY, LOG_DAEMON); //открываем системный журнал

    if(argc != 2){
        std::cerr << "Usage: " << argv[0] << " <config.file>\n";
        closelog();
        return 1;
    }

    try 
    {
        const int result = Daemon::getInstance().run(argv[1]);

        closelog();

        return result;
    }
    catch(const std::exception& e){
        syslog(
            LOG_ERR,
            "Startup error: %s",
            e.what()
        );
        std::cerr << "Error: " << e.what() << '\n';
        closelog();
        return EXIT_FAILURE;
    }
}
