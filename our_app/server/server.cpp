#include "server.h"

// #include "logger.h"

void Server::run() {
    for (;;) {
        std::vector<Incoming> incoming = net_.wait();  // 1. network turn
        buffering_turn(incoming);                      // 2. buffering turn
        processing_turn();                             // 3. processing turn
    }
}

// Bytes -> complete commands, queued in arrival order.
void Server::buffering_turn(const std::vector<Incoming>& incoming) {
    for (const Incoming& in : incoming) {
        if (in.closed) { inbuf_.erase(in.id); continue; }

        std::string& buf = inbuf_[in.id];  // created on first data from this client
        buf += in.bytes;

        std::vector<comproto::base_com*> msgs;
        int bad = comproto::frame::extract_frames(buf, msgs, comproto::frame::direction::to_server);
        for (comproto::base_com* m : msgs) pending_.push_back({in.id, m});
        // if (bad > 0) logmsg("client " + std::to_string(in.id) + ": discarded " + std::to_string(bad) + " bad frame(s)");
    }
}

void Server::processing_turn() {
    while (!pending_.empty()) {
        auto [from, msg] = pending_.front();
        pending_.pop_front();
        handle(from, msg);
        delete msg;
    }
}

void Server::handle(comproto::ClientID from, comproto::base_com* msg) {
    using namespace comproto::command;
    // logmsg("client " + std::to_string(from) + " RECV " + msg->to_string());

    if (auto* sm = dynamic_cast<comproto::command::sendmsg*>(msg)) {
        store_[sm->recipient].push_back({sm->sender, sm->message});
        reply(from, "OK");

    } else if (auto* gm = dynamic_cast<getmsg*>(msg)) {
        auto it = store_.find(gm->user);
        if (it == store_.end()) {
            reply(from, "NOMSG");
        } else {
            StoredMsg m = it->second.front();
            it->second.pop_front();
            if (it->second.empty()) store_.erase(it);  // keeps STATUSREQ exact
            reply(from, m.from + "," + m.text);
        }

    } else if (dynamic_cast<statusreq*>(msg)) {
        std::string out;
        for (const auto& [user, q] : store_) {
            if (!out.empty()) out += ',';
            out += user + ":" + std::to_string(q.size());
        }
        reply(from, out.empty() ? "EMPTY" : out);
    }
}

void Server::reply(comproto::ClientID to, const std::string& text) {
    comproto::response r(text);
    net_.send_to(to, comproto::frame::encode(&r));
    // logmsg("client " + std::to_string(to) + " SEND " + text);
}