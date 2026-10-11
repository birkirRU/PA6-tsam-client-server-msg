#include "../common.h"
#include "server.h"

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    Server server(tool::parse_port(argv[1], "port"));
    server.run();
}