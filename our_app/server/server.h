#pragma once
#include <deque>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../common.h"
#include "network.h"

class Server {
public:
    explicit Server(uint16_t port) : net_(port) {}
    void run();  // the main loop

private:
    struct StoredMsg {
        std::string from;
        std::string text;
    };

    void buffering_turn(const std::vector<Incoming>& incoming);
    void processing_turn();
    void handle(comproto::ClientID from, comproto::base_com* msg);
    void reply(comproto::ClientID to, const std::string& text);

    Network net_;
    std::unordered_map<comproto::ClientID, std::string> inbuf_;  // per client: bytes not yet forming a full frame
    std::deque<std::pair<comproto::ClientID, comproto::base_com*>> pending_;  // decoded commands, arrival order
    std::unordered_map<std::string, std::deque<StoredMsg>> store_;           // username -> messages
};