#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>

#include "common.h"
#include "server.h"




int main(int argc, char** argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s <port>\n", argv[0]); return 1; }

	unsigned int listening_port = tool::parse_port(argv[2], "listening port");


    // Create main listening socket that accepts new client connections

    int listening_fd = socket(AF_INET, SOCK_STREAM, 0);

    // DEBUG: SO_REUSEADDR set for debugging and testing,
    // without it TIME_WAIT would hinder usage of same port after a restart of server.
    int yes = 1;
    setsockopt(listening_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    // Listen on all interfaces (e.g. localhost 127.0.0.1 and
    // outward facing ip's like tsam server 130.208.246.98)
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(atoi(argv[1])));
    if (bind(listening_fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) { perror("bind"); return 1; }
    if (listen(listening_fd, SOMAXCONN) < 0) { perror("listen"); return 1; }

}