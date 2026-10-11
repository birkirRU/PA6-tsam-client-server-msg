#include "network.h"

#include <arpa/inet.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>

// #include "logger.h"

Network::Network(uint16_t port) {
    listen_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    int yes = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) { perror("bind"); exit(1); }
    listen(listen_fd_, SOMAXCONN);

    poller_.add(listen_fd_);
    // logmsg("listening on port " + std::to_string(port));
}

std::vector<Incoming> Network::wait() {
    std::vector<Incoming> incoming;

    for (const PollEvent& e : poller_.wait()) {
        if (e.fd == listen_fd_) {
            // Level-triggered: if more clients are waiting we are told again, so one accept is enough.
            int fd = accept4(listen_fd_, nullptr, nullptr, SOCK_NONBLOCK);
            if (fd < 0) continue;
            comproto::ClientID id = next_id_++;
            conns_[id] = Conn{fd, "", false};
            id_of_[fd] = id;
            poller_.add(fd);
            // logmsg("client " + std::to_string(id) + " connected");
            continue;
        }

        auto it = id_of_.find(e.fd);
        if (it == id_of_.end()) continue;
        comproto::ClientID id = it->second;

        if (e.readable) {
            char buf[4096];
            ssize_t n = recv(e.fd, buf, sizeof buf, 0);
            if (n > 0) {
                incoming.push_back({id, std::string(buf, n), false});
            } else if (n == 0 || (errno != EAGAIN && errno != EINTR)) {
                close_conn(id);
                incoming.push_back({id, "", true});
                continue;
            }
        }
        if (e.writable) flush(id);
    }
    return incoming;
}

void Network::send_to(comproto::ClientID id, const std::vector<char>& bytes) {
    auto it = conns_.find(id);
    if (it == conns_.end()) return;  // client already gone
    it->second.out.append(bytes.data(), bytes.size());
    flush(id);
}

// Try to send what is pending; only watch for "writable" while something is left over.
void Network::flush(comproto::ClientID id) {
    Conn& c = conns_[id];
    if (!c.out.empty()) {
        ssize_t n = send(c.fd, c.out.data(), c.out.size(), MSG_NOSIGNAL);
        if (n > 0) c.out.erase(0, n);
    }
    bool want = !c.out.empty();
    if (want != c.want_write) {
        poller_.watch_write(c.fd, want);
        c.want_write = want;
    }
}

void Network::close_conn(comproto::ClientID id) {
    int fd = conns_[id].fd;
    poller_.remove(fd);
    close(fd);
    id_of_.erase(fd);
    conns_.erase(id);
    // logmsg("client " + std::to_string(id) + " disconnected");
}