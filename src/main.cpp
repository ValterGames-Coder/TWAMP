#include "Daemonize.hpp"
#include "TwampControlServer.hpp"
#include <syslog.h>

int main() {
    Daemonize::run("/run/twamp-server.pid");
    openlog("twamp-server", LOG_PID, LOG_DAEMON);
    syslog(LOG_INFO, "Daemon started");

    TwampControlServer server(862);
    server.run();
    syslog(LOG_INFO, "Daemon stopping");
    closelog();
    return 0;
}