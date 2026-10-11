#pragma once
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
class Network {
public:
    explicit Network(uint16_t port);

    // Network turn: sleeps until something happens; accepts new clients, reads their bytes.
    std::vector<Incoming> wait();

    // Outwards: send bytes to a client (the part that doesn't fit now goes out later).
    void send_to(comproto::ClientID id, const std::vector<char>& bytes);

private:
    struct Conn {
        int fd;
        std::string out;          // bytes still owed to this client
        bool want_write = false;
    };

    void flush(comproto::ClientID id);
    void close_conn(comproto::ClientID id);

    Poller poller_;
    int listen_fd_;
    comproto::ClientID next_id_ = 1;
    std::unordered_map<comproto::ClientID, Conn> conns_;
    std::unordered_map<int, comproto::ClientID> id_of_;  // fd -> client id
};