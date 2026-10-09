// T-4O9-TSAM protocol version 3.2
//
// epoll_demo.cpp - smallest useful single-threaded epoll server (echoes bytes back).
// Linux only (raw epoll, no wrapper) - the point is to see the three calls working together.
//
// Build: g++ -std=c++17 -Wall -Wextra -o epoll_demo epoll_demo.cpp
// Run:   ./epoll_demo 4044
// Try:   nc 127.0.0.1 4044      (open several terminals, type, disconnect, reconnect)

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

// Per-connection state. Existence in the map == "this fd is a live client".
struct Conn {
    std::string out;          // bytes we still owe this client
    bool want_write = false;  // are we currently watching EPOLLOUT for it?
};

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main(int argc, char** argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s <port>\n", argv[0]); return 1; }
    signal(SIGPIPE, SIG_IGN);  // writing to a closed peer must not kill us

    // STEP 1: listening socket, non-blocking.
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(atoi(argv[1])));
    if (bind(lfd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) { perror("bind"); return 1; }
    if (listen(lfd, SOMAXCONN) < 0) { perror("listen"); return 1; }
    set_nonblocking(lfd);

    // STEP 2: create the kernel watch-list.
    int ep = epoll_create1(0);
    if (ep < 0) { perror("epoll_create1"); return 1; }

    // STEP 3: put the listening fd on it. "Readable" on this fd means "a client is connecting".
    epoll_event lev{};
    lev.events = EPOLLIN;
    lev.data.fd = lfd;
    epoll_ctl(ep, EPOLL_CTL_ADD, lfd, &lev);

    std::unordered_map<int, Conn> conns;

    // Change what we watch for on a client: always EPOLLIN, EPOLLOUT only while output is pending.
    auto set_write_interest = [&](int fd, bool on) {
        Conn& c = conns[fd];
        if (c.want_write == on) return;  // avoid pointless syscalls
        epoll_event ev{};
        ev.events = EPOLLIN | (on ? EPOLLOUT : 0u);
        ev.data.fd = fd;
        epoll_ctl(ep, EPOLL_CTL_MOD, fd, &ev);
        c.want_write = on;
    };

    // Send as much pending output as the socket accepts. Returns false if the connection broke.
    auto flush = [&](int fd) -> bool {
        Conn& c = conns[fd];
        while (!c.out.empty()) {
            ssize_t s = send(fd, c.out.data(), c.out.size(), 0);
            if (s > 0) c.out.erase(0, static_cast<size_t>(s));
            else if (s < 0 && errno == EINTR) continue;
            else if (s < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;  // buffer full
            else return false;
        }
        set_write_interest(fd, !c.out.empty());  // STEP 6: arm EPOLLOUT only if data is left
        return true;
    };

    // STEP 7: disconnect = remove from watch-list, close, forget state (in that order).
    auto drop = [&](int fd) {
        epoll_ctl(ep, EPOLL_CTL_DEL, fd, nullptr);
        close(fd);
        conns.erase(fd);
        printf("fd %d disconnected (%zu clients)\n", fd, conns.size());
    };

    epoll_event events[64];  // output array: the kernel fills the first n entries
    for (;;) {
        // STEP 4: sleep (zero CPU) until something on the list is ready.
        int n = epoll_wait(ep, events, 64, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        // STEP 5: dispatch on the cookie (fd) we stored when registering.
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            uint32_t what = events[i].events;

            if (fd == lfd) {  // new client(s)
                for (;;) {
                    int cfd = accept(lfd, nullptr, nullptr);
                    if (cfd < 0) break;  // EAGAIN: no more waiting right now
                    set_nonblocking(cfd);
                    epoll_event cev{};
                    cev.events = EPOLLIN;
                    cev.data.fd = cfd;
                    epoll_ctl(ep, EPOLL_CTL_ADD, cfd, &cev);
                    conns[cfd] = Conn{};
                    printf("fd %d connected (%zu clients)\n", cfd, conns.size());
                }
                continue;
            }

            if (conns.find(fd) == conns.end()) continue;  // already dropped this round

            bool dead = false;

            if (what & EPOLLIN) {
                char buf[4096];
                ssize_t r = recv(fd, buf, sizeof buf, 0);  // one read per event keeps things fair
                if (r > 0) {
                    printf("fd %d: %zd bytes\n", fd, r);
                    conns[fd].out.append(buf, static_cast<size_t>(r));  // echo
                    if (!flush(fd)) dead = true;
                } else if (r == 0) {
                    dead = true;  // orderly shutdown by the client
                } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                    dead = true;
                }
            }
            if (!dead && (what & EPOLLOUT)) {
                if (!flush(fd)) dead = true;
            }
            // Error/hang-up with nothing left to read: connection is finished.
            if (!dead && (what & (EPOLLERR | EPOLLHUP)) && !(what & EPOLLIN)) dead = true;

            if (dead) drop(fd);
        }
    }
    close(ep);
    close(lfd);
    return 0;
}