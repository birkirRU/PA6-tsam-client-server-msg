#include <cstdio>
#include "common.h"
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <cerrno>
#include <string>
#include <sys/socket.h>
#include <csignal>

int main(int argc, char* argv[])
{
    signal(SIGPIPE, SIG_IGN);   //dead server makes send() return -1 instead of killing

    if (argc != 3){
        fprintf(stderr, "usage: %s <ip> <port>\n", argv[0]);
        return 1;
    }

    unsigned int port = tool::parse_port(argv[2], "port");

    struct addrinfo hints{};
    hints.ai_family = AF_INET; //IPv4
    hints.ai_socktype = SOCK_STREAM; // TCP
    
    struct  addrinfo* res = nullptr;
    int rc = getaddrinfo(argv[1], argv[2], &hints, &res);
    if (rc != 0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));    
        return 1;
    }
    int sock = socket(res->ai_family, res->ai_socktype, 0);
    if (sock < 0){
        perror("socket");
        freeaddrinfo(res);
        return 1;
    }

    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0){
        perror("connect");
        close(sock);
        freeaddrinfo(res);
        return 1;
    }

    freeaddrinfo(res);
    printf("Connected to %s:%u\n", argv[1], port);

    struct pollfd fds[2];
    fds[0].fd = STDIN_FILENO; //terminal
    fds[0].events = POLLIN; //wake when there is input to read
    fds[1].fd = sock; //server connection
    fds[1].events = POLLIN; //wake when there is something
    
    std::string input;

    while (true){
        int n = poll(fds, 2, -1);
        if (n < 0){
            if (errno == EINTR) continue; 
            perror("poll");
            break;
        }

        if (fds[0].revents & POLLIN) {
            char tmp[1024];
            ssize_t r = read(STDIN_FILENO, tmp, sizeof tmp);
            if (r <= 0) {                    // <0 = error
                printf("stdin closed, exiting\n");
                break;
            }
            input.append(tmp, r);

            size_t nl;
            while ((nl = input.find('\n')) != std::string::npos) {
                std::string line = input.substr(0, nl);   // one complete line, without '\n'
                input.erase(0, nl + 1);                   // remove it, including the '\n'
                if (line.empty()) continue;               // ignore empty Enter presses
                std::vector<char> frame = comproto::frame::encode_payload(line);
                if (frame.empty()) {
                    printf("command too long (max %zu bytes), not sent\n", comproto::frame::MAX_PAYLOAD);
                    continue;
                }
                if (!sendAll(sock, frame)) {
                    perror("send");
                    break;
                }
                printf("SENT: %s\n", line.c_str());
            }
        }

        if (fds[1].revents & (POLLIN | POLLHUP | POLLERR)){
            printf("socket is ready\n");
        }
    }
    close(sock);
}
    

