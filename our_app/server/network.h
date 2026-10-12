#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "../common.h"
#include "poller.h"

// What the network turn hands to the server: bytes from a client, or "that client is gone".
struct Incoming {
    comproto::ClientID id;
    std::string bytes;
    bool closed = false;
};

// Sockets and bytes only. Knows nothing about the protocol.
//
// Error policy:
//   - startup failures (socket/bind/listen/epoll) are fatal: logged, exit(1);
//   - a failure on one client's socket drops only that client, with the reason logged;
//   - transient conditions (EINTR, EAGAIN, a client leaving before accept) are ignored.
class Network {
public:
    explicit Network(uint16_t port);
    ~Network();

    // Network turn: sleeps until something happens; accepts new clients, reads their bytes.
    std::vector<Incoming> wait();

    // Outwards: queue bytes for a client and send what fits now (the rest goes out later).
    // false if the client is gone or its connection failed (the bytes are dropped).
    bool send_to(comproto::ClientID id, const std::vector<char>& bytes);

    size_t client_count() const { return conns_.size(); }

private:
    struct Conn {
        int fd;
        std::string out;          // bytes still owed to this client
        bool want_write = false;
    };

    void accept_client();
    void read_from(comproto::ClientID id, std::vector<Incoming>& incoming);
    bool flush(comproto::ClientID id);
    void close_conn(comproto::ClientID id, const std::string& reason);
    void pause_accepting();
    void resume_accepting_if_due();

    Poller poller_;
    int listen_fd_ = -1;
    int spare_fd_ = -1;  // reserved descriptor, released to shed a connection when out of fds
    bool accepting_ = true;
    std::chrono::steady_clock::time_point resume_at_;
    comproto::ClientID next_id_ = 1;
    std::unordered_map<comproto::ClientID, Conn> conns_;
    std::unordered_map<int, comproto::ClientID> id_of_;  // fd -> client id
    std::vector<comproto::ClientID> closed_;            // dropped outside wait(), reported by the next wait()
};
