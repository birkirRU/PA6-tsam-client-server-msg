#pragma once
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../common.h"
#include "network.h"

class Server {
public:
    explicit Server(uint16_t port) : net_(port) {}

    // The main loop. Returns after request_stop() was called (e.g. from a signal handler).
    void run();

    // Async-signal-safe: asks run() to return at the next opportunity.
    static void request_stop(int signal_number);

private:
    struct StoredMsg {
        std::string from;
        std::string text;
    };

    void buffering_turn(const std::vector<Incoming>& incoming);
    void processing_turn();
    void disconnect_turn();

    void handle(comproto::ClientID from, const std::string& payload);
    void handle_sendmsg(comproto::ClientID from, const comproto::command::sendmsg& cmd);
    void handle_getmsg(comproto::ClientID from, const comproto::command::getmsg& cmd);
    void handle_statusreq(comproto::ClientID from);
    void handle_listusers(comproto::ClientID from);
    void handle_login(comproto::ClientID from, const comproto::command::login& cmd);

    void reply(comproto::ClientID to, const comproto::response& rep);
    std::string state_summary() const;

    Network net_;
    std::unordered_map<comproto::ClientID, std::string> inbuf_;  // per client: bytes not yet forming a full frame
    std::deque<std::pair<comproto::ClientID, std::string>> pending_;  // complete payloads, arrival order
    std::vector<comproto::ClientID> closed_;                     // connections that ended this turn

    std::unordered_map<comproto::ClientID, std::string> uid_of_;  // logged-in connections -> UID
    std::set<std::string> known_users_;                           // every UID ever handed out
    std::map<std::string, std::deque<StoredMsg>> store_;          // UID -> messages, oldest first
    size_t stored_total_ = 0;                                     // sum of all queue sizes in store_
};
