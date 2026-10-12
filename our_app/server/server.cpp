#include "server.h"

#include <algorithm>

#include "logger.h"

using namespace comproto;

void Server::run() {
    for (;;) {
        std::vector<Incoming> incoming = net_.wait();  // network turn
        buffering_turn(incoming);
        processing_turn();
        disconnect_turn();
    }
}

// Bytes -> complete payloads, queued in arrival order.
// Disconnects are only recorded here: commands a client sent just before
// leaving are still processed (e.g. a final SENDMSG).
void Server::buffering_turn(const std::vector<Incoming>& incoming) {
    for (const Incoming& in : incoming) {
        if (in.closed) { closed_.push_back(in.id); continue; }

        std::string& buf = inbuf_[in.id];  // created on first data from this client
        buf += in.bytes;

        std::vector<std::string> payloads;
        std::vector<std::string> problems;
        frame::extract_frames(buf, payloads, &problems);
        for (std::string& p : payloads) pending_.push_back({in.id, std::move(p)});
        for (const std::string& why : problems)
            logmsg("client " + std::to_string(in.id) + ": discarded bytes: " + why);
    }
}

void Server::processing_turn() {
    while (!pending_.empty()) {
        auto [from, payload] = std::move(pending_.front());
        pending_.pop_front();
        handle(from, payload);
    }
}

// Forget everything tied to connections that ended. Stored messages stay.
void Server::disconnect_turn() {
    for (ClientID id : closed_) {
        inbuf_.erase(id);
        uid_of_.erase(id);
    }
    closed_.clear();
}

void Server::handle(ClientID from, const std::string& payload) {
    logmsg("client " + std::to_string(from) + " RECV " + payload);

    std::unique_ptr<base_com> cmd = decode_command(payload);
    if (cmd == nullptr) {
        reply(from, reply::error());
        return;
    }

    if (auto* c = dynamic_cast<command::sendmsg*>(cmd.get()))        handle_sendmsg(from, *c);
    else if (auto* c = dynamic_cast<command::getmsg*>(cmd.get()))    handle_getmsg(from, *c);
    else if (dynamic_cast<command::statusreq*>(cmd.get()))           handle_statusreq(from);
    else if (dynamic_cast<command::listusers*>(cmd.get()))           handle_listusers(from);
    else if (auto* c = dynamic_cast<command::login*>(cmd.get()))     handle_login(from, *c);
    else                                                             reply(from, reply::error());
}

// Only users that have logged in at some point can receive messages;
// they do not have to be online.
void Server::handle_sendmsg(ClientID from, const command::sendmsg& cmd) {
    if (known_users_.count(cmd.recipient) == 0) {
        reply(from, reply::sendmsg(false, ""));
        return;
    }
    store_[cmd.recipient].push_back({cmd.sender, cmd.message});
    reply(from, reply::sendmsg(true, cmd.recipient));
}

void Server::handle_getmsg(ClientID from, const command::getmsg& cmd) {
    auto it = store_.find(cmd.user);
    if (it == store_.end()) {
        reply(from, reply::getmsg());
        return;
    }
    StoredMsg m = std::move(it->second.front());
    it->second.pop_front();
    if (it->second.empty()) store_.erase(it);  // keeps STATUSREQ exact
    reply(from, reply::getmsg(m.text));
}

void Server::handle_statusreq(ClientID from) {
    reply::statusreq rep;
    for (const auto& [uid, q] : store_) rep.counts.push_back({uid, q.size()});
    reply(from, rep);
}

void Server::handle_listusers(ClientID from) {
    reply::listusers rep;
    for (const auto& [id, uid] : uid_of_) rep.users.push_back(uid);
    std::sort(rep.users.begin(), rep.users.end());
    reply(from, rep);
}

// Each connection logs in once; its UID is the chosen name plus the connection id.
void Server::handle_login(ClientID from, const command::login& cmd) {
    auto it = uid_of_.find(from);
    if (it != uid_of_.end()) {
        reply(from, reply::login(false, it->second));
        return;
    }
    std::string uid = make_uid(cmd.user_name, from);
    uid_of_[from] = uid;
    known_users_.insert(uid);
    reply(from, reply::login(true, uid));
}

void Server::reply(ClientID to, const response& rep) {
    net_.send_to(to, frame::encode(&rep));
    logmsg("client " + std::to_string(to) + " SEND " + rep.to_string());
}
