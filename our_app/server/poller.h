#pragma once
#include <sys/epoll.h>
#include <unistd.h>

#include <vector>

struct PollEvent {
    int fd;
    bool readable;
    bool writable;
};

// Thin wrapper over epoll (Linux only). Always watches for readable; writable is optional.
class Poller {
public:
    Poller() : ep_(epoll_create1(0)) {}
    ~Poller() { close(ep_); }

    void add(int fd)                  { ctl(EPOLL_CTL_ADD, fd, false); }
    void watch_write(int fd, bool on) { ctl(EPOLL_CTL_MOD, fd, on); }
    void remove(int fd)               { epoll_ctl(ep_, EPOLL_CTL_DEL, fd, nullptr); }

    // Sleeps until something is ready (zero CPU). May return empty if interrupted by a signal.
    std::vector<PollEvent> wait() {
        epoll_event evs[64];
        int n = epoll_wait(ep_, evs, 64, -1);
        std::vector<PollEvent> out;
        for (int i = 0; i < n; ++i) {
            uint32_t e = evs[i].events;
            out.push_back({evs[i].data.fd, (e & (EPOLLIN | EPOLLHUP | EPOLLERR)) != 0, (e & EPOLLOUT) != 0});
        }
        return out;
    }

private:
    void ctl(int op, int fd, bool write) {
        epoll_event ev{};
        ev.events = EPOLLIN | (write ? EPOLLOUT : 0u);
        ev.data.fd = fd;
        epoll_ctl(ep_, op, fd, &ev);
    }
    int ep_;
};