#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdio>

#include "../common.h"
#include "logger.h"
#include "server.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return 1;
    }
    unsigned int port = tool::parse_port(argv[1], "port");

    if (!serverlog::open("server.log")) {
        perror("cannot open server.log");
        return 1;
    }

    logmsg("tsamserver starting: pid " + std::to_string(getpid()) + ", port " + std::to_string(port));

    // SIGINT (Ctrl-C) and SIGTERM end the server cleanly with a final log line.
    // No SA_RESTART, so the signal interrupts the sleep in epoll_wait.
    struct sigaction sa{};
    sa.sa_handler = Server::request_stop;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, nullptr) < 0 || sigaction(SIGTERM, &sa, nullptr) < 0)
        logmsg(errstr("sigaction failed, Ctrl-C will not log a shutdown line", errno));

    Server server(static_cast<uint16_t>(port));
    server.run();
    return 0;
}
