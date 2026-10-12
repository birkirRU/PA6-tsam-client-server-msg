#include "network.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>

#include "logger.h"

namespace {
    // How long to stop accepting after running out of resources, instead of spinning.
    const auto ACCEPT_PAUSE = std::chrono::seconds(1);

    // close() that reports errors. Never retried: on Linux the fd is released even
    // when close() fails (including EINTR), so a retry could close someone else's fd.
    void close_fd(int fd) {
        if (close(fd) < 0 && errno != EINTR) logmsg(errstr("close", errno));
    }

    int open_spare_fd() { return open("/dev/null", O_RDONLY); }
}

Network::Network(uint16_t port) {
    if (!poller_.ok()) fatal(errstr("epoll_create1", errno));

    listen_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (listen_fd_ < 0) fatal(errstr("socket", errno));

    // Allows restarting the server right away instead of waiting out TIME_WAIT.
    int yes = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0)
        logmsg(errstr("setsockopt(SO_REUSEADDR) failed, continuing", errno));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0)
        fatal(errstr(("bind to port " + std::to_string(port)).c_str(), errno));
    if (listen(listen_fd_, SOMAXCONN) < 0) fatal(errstr("listen", errno));
    if (!poller_.add(listen_fd_)) fatal(errstr("epoll_ctl(ADD listening socket)", errno));

    spare_fd_ = open_spare_fd();
    if (spare_fd_ < 0) logmsg(errstr("could not reserve a spare descriptor", errno));

    logmsg("listening on port " + std::to_string(port));
}

Network::~Network() {
    for (auto& [id, c] : conns_) close_fd(c.fd);
    if (spare_fd_ >= 0) close_fd(spare_fd_);
    if (listen_fd_ >= 0) close_fd(listen_fd_);
}

std::vector<Incoming> Network::wait() {
    std::vector<Incoming> incoming;

    // Connections dropped since the last turn (e.g. a failed send) are reported first,
    // without sleeping.
    for (comproto::ClientID id : closed_) incoming.push_back({id, "", true});
    closed_.clear();

    resume_accepting_if_due();
    int timeout = !incoming.empty() ? 0 : (accepting_ ? -1 : 1000);

    std::vector<PollEvent> events;
    if (!poller_.wait(events, timeout)) fatal(errstr("epoll_wait", errno));

    for (const PollEvent& e : events) {
        if (e.fd == listen_fd_) {
            accept_client();
            continue;
        }

        auto it = id_of_.find(e.fd);
        if (it == id_of_.end()) continue;  // closed earlier in this turn
        comproto::ClientID id = it->second;

        if (e.readable) read_from(id, incoming);
        if (e.writable && conns_.count(id) && !flush(id)) incoming.push_back({id, "", true});
    }
    return incoming;
}

// Level-triggered: if more clients are waiting we are told again, so one accept is enough.
void Network::accept_client() {
    int fd = accept4(listen_fd_, nullptr, nullptr, SOCK_NONBLOCK);
    if (fd < 0) {
        int err = errno;
        switch (err) {
        case EAGAIN:
#if EAGAIN != EWOULDBLOCK
        case EWOULDBLOCK:
#endif
        case EINTR:
            return;  // nothing to accept after all
        case ECONNABORTED:
        case EPROTO:
            logmsg("a client left before its connection was accepted");
            return;
        case EMFILE:
        case ENFILE:
            // Out of descriptors. The pending connection keeps the listening socket readable,
            // so ignoring it would spin. Use the spare descriptor to accept and close it.
            if (spare_fd_ >= 0) {
                close_fd(spare_fd_);
                int shed = accept4(listen_fd_, nullptr, nullptr, SOCK_NONBLOCK);
                if (shed >= 0) close_fd(shed);
                spare_fd_ = open_spare_fd();
                logmsg(errstr("rejected a connection", err));
            } else {
                logmsg(errstr("accept", err));
                pause_accepting();
            }
            return;
        default:  // ENOBUFS, ENOMEM, ...: back off instead of spinning
            logmsg(errstr("accept", err));
            pause_accepting();
            return;
        }
    }

    if (!poller_.add(fd)) {
        logmsg(errstr("epoll_ctl(ADD new client) failed, connection refused", errno));
        close_fd(fd);
        return;
    }

    comproto::ClientID id = next_id_++;
    conns_[id] = Conn{fd, "", false};
    id_of_[fd] = id;
    logmsg("client " + std::to_string(id) + " connected (" + std::to_string(conns_.size()) + " connected)");
}

void Network::read_from(comproto::ClientID id, std::vector<Incoming>& incoming) {
    char buf[4096];
    ssize_t n = recv(conns_[id].fd, buf, sizeof buf, 0);
    if (n > 0) {
        incoming.push_back({id, std::string(buf, n), false});
        return;
    }
    if (n == 0) {
        close_conn(id, "closed the connection");
    } else if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return;
    } else {
        close_conn(id, errstr("recv", errno));
    }
    incoming.push_back({id, "", true});
}

bool Network::send_to(comproto::ClientID id, const std::vector<char>& bytes) {
    auto it = conns_.find(id);
    if (it == conns_.end()) return false;  // client already gone
    it->second.out.append(bytes.data(), bytes.size());
    if (!flush(id)) {
        closed_.push_back(id);  // reported to the server by the next wait()
        return false;
    }
    return true;
}

// Sends what is pending; watches for "writable" only while something is left over.
// false = the connection failed and has been closed (queued output is dropped).
bool Network::flush(comproto::ClientID id) {
    Conn& c = conns_[id];
    while (!c.out.empty()) {
        ssize_t n = send(c.fd, c.out.data(), c.out.size(), MSG_NOSIGNAL);
        if (n > 0) { c.out.erase(0, n); continue; }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;  // socket buffer full
        close_conn(id, errstr("send", errno));
        return false;
    }

    bool want = !c.out.empty();
    if (want != c.want_write) {
        if (!poller_.watch_write(c.fd, want)) {
            close_conn(id, errstr("epoll_ctl(MOD)", errno));
            return false;
        }
        c.want_write = want;
        if (want) logmsg("client " + std::to_string(id) + ": " + std::to_string(c.out.size()) +
                         " bytes queued, waiting for the client to read");
        else      logmsg("client " + std::to_string(id) + ": queued output fully sent");
    }
    return true;
}

void Network::close_conn(comproto::ClientID id, const std::string& reason) {
    auto it = conns_.find(id);
    if (it == conns_.end()) return;
    int fd = it->second.fd;
    if (!poller_.remove(fd)) logmsg(errstr("epoll_ctl(DEL)", errno));
    close_fd(fd);
    id_of_.erase(fd);
    conns_.erase(it);
    logmsg("client " + std::to_string(id) + " disconnected: " + reason +
           " (" + std::to_string(conns_.size()) + " connected)");
}

void Network::pause_accepting() {
    if (!accepting_) return;
    if (!poller_.remove(listen_fd_)) fatal(errstr("epoll_ctl(DEL listening socket)", errno));
    accepting_ = false;
    resume_at_ = std::chrono::steady_clock::now() + ACCEPT_PAUSE;
    logmsg("not accepting new connections for 1 second");
}

void Network::resume_accepting_if_due() {
    if (accepting_ || std::chrono::steady_clock::now() < resume_at_) return;
    if (!poller_.add(listen_fd_)) fatal(errstr("epoll_ctl(ADD listening socket)", errno));
    accepting_ = true;
    if (spare_fd_ < 0) spare_fd_ = open_spare_fd();
    logmsg("accepting new connections again");
}
