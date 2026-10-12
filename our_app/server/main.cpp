#include <cerrno>
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

    Server server(static_cast<uint16_t>(port));
    server.run();
}
