#pragma once
#include <sys/epoll.h>
#include <unistd.h>

#include <cerrno>
#include <vector>

struct PollEvent {
    int fd;
    bool readable;
    bool writable;
};

// Thin wrapper over epoll (Linux only). Always watches for readable; writable is optional.
// Every operation reports failure through its return value and errno.
class Poller {
public:
    Poller() : ep_(epoll_create1(0)) {}
    ~Poller() { if (ep_ >= 0) close(ep_); }
    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;

    bool ok() const { return ep_ >= 0; }  // false: epoll_create1 failed (errno set)

    bool add(int fd)                  { return ctl(EPOLL_CTL_ADD, fd, false); }
    bool watch_write(int fd, bool on) { return ctl(EPOLL_CTL_MOD, fd, on); }
    bool remove(int fd)               { return epoll_ctl(ep_, EPOLL_CTL_DEL, fd, nullptr) == 0; }

    // Sleeps until something is ready (zero CPU), or until timeout_ms passes (-1 = forever).
    // Returns false only on a real error; an interrupting signal is a normal, empty return.
    bool wait(std::vector<PollEvent>& out, int timeout_ms) {
        epoll_event evs[64];
        int n = epoll_wait(ep_, evs, 64, timeout_ms);
        if (n < 0) return errno == EINTR;
        for (int i = 0; i < n; ++i) {
            uint32_t e = evs[i].events;
            out.push_back({evs[i].data.fd, (e & (EPOLLIN | EPOLLHUP | EPOLLERR)) != 0, (e & EPOLLOUT) != 0});
        }
        return true;
    }

private:
    bool ctl(int op, int fd, bool write) {
        epoll_event ev{};
        ev.events = EPOLLIN | (write ? EPOLLOUT : 0u);
        ev.data.fd = fd;
        return epoll_ctl(ep_, op, fd, &ev) == 0;
    }
    int ep_;
};
