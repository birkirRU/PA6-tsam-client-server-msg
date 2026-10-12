#include "server.h"

#include <algorithm>
#include <csignal>
#include <exception>

#include "logger.h"

using namespace comproto;

namespace {
    volatile std::sig_atomic_t stop_signal = 0;

    std::string client_tag(ClientID id) { return "client " + std::to_string(id); }
}

void Server::request_stop(int signal_number) { stop_signal = signal_number; }

// A C++ exception (e.g. std::bad_alloc) while serving one command is logged and the
// loop continues; only that command is lost. Fatal system errors exit via fatal().
// A stop request interrupts the sleep in net_.wait() (EINTR), so it is seen at once.
void Server::run() {
    while (stop_signal == 0) {
        try {
            std::vector<Incoming> incoming = net_.wait();  // network turn
            buffering_turn(incoming);
            processing_turn();
            disconnect_turn();
        } catch (const std::exception& e) {
            logmsg(std::string("ERROR exception in main loop: ") + e.what());
        }
    }
    logmsg("shutting down on signal " + std::to_string(stop_signal) + ": " + state_summary() +
           " (stored messages are not kept)");
}

// "3 connected, 2 logged in, 5 message(s) stored for 2 user(s)"
std::string Server::state_summary() const {
    return std::to_string(net_.client_count()) + " connected, " +
           std::to_string(uid_of_.size()) + " logged in, " +
           std::to_string(stored_total_) + " message(s) stored for " +
           std::to_string(store_.size()) + " user(s)";
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
            logmsg(client_tag(in.id) + ": discarded bytes: " + why);
    }
}

void Server::processing_turn() {
    while (!pending_.empty()) {
        auto [from, payload] = std::move(pending_.front());
        pending_.pop_front();
        try {
            handle(from, payload);
        } catch (const std::exception& e) {
            logmsg(client_tag(from) + ": ERROR exception while handling command: " + e.what());
        }
    }
}

// Forget everything tied to connections that ended. Stored messages stay.
void Server::disconnect_turn() {
    for (ClientID id : closed_) {
        auto buf = inbuf_.find(id);
        if (buf != inbuf_.end()) {
            if (!buf->second.empty())
                logmsg(client_tag(id) + ": dropped " + std::to_string(buf->second.size()) +
                       " bytes of an incomplete frame");
            inbuf_.erase(buf);
        }
        auto user = uid_of_.find(id);
        if (user != uid_of_.end()) {
            std::string uid = user->second;
            uid_of_.erase(user);
            logmsg("STATE " + uid + " logged out; " + state_summary());
        }
    }
    closed_.clear();
}

void Server::handle(ClientID from, const std::string& payload) {
    logmsg(client_tag(from) + " RECV " + printable(payload));

    std::unique_ptr<base_com> cmd = decode_command(payload);
    if (cmd == nullptr) {
        std::string name = payload.substr(0, payload.find(','));
        if (is_known_command(name))
            logmsg(client_tag(from) + ": rejected malformed " + name + " command");
        else
            logmsg(client_tag(from) + ": rejected unknown command '" + printable(name) + "'");
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
        logmsg(client_tag(from) + ": SENDMSG not stored, " + cmd.recipient + " has never logged in");
        reply(from, reply::sendmsg(false, ""));
        return;
    }
    std::deque<StoredMsg>& q = store_[cmd.recipient];
    q.push_back({cmd.sender, cmd.message});
    stored_total_++;
    logmsg("STATE stored message from " + cmd.sender + " for " + cmd.recipient + " (" +
           std::to_string(q.size()) + " waiting for them); " + state_summary());
    reply(from, reply::sendmsg(true, cmd.recipient));
}

void Server::handle_getmsg(ClientID from, const command::getmsg& cmd) {
    auto it = store_.find(cmd.user);
    if (it == store_.end()) {
        logmsg(client_tag(from) + ": no message stored for " + cmd.user);
        reply(from, reply::getmsg());
        return;
    }
    StoredMsg m = std::move(it->second.front());
    it->second.pop_front();
    stored_total_--;
    size_t left = it->second.size();
    if (left == 0) store_.erase(it);  // keeps STATUSREQ exact
    logmsg("STATE delivered and deleted message from " + m.from + " for " + cmd.user + " (" +
           std::to_string(left) + " left for them); " + state_summary());
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
    logmsg("STATE " + client_tag(from) + " logged in as " + uid + "; " + state_summary());
    reply(from, reply::login(true, uid));
}

// Logs SEND only when the reply was actually handed to the connection.
void Server::reply(ClientID to, const response& rep) {
    std::string text = rep.to_string();
    std::vector<char> bytes = frame::encode_payload(text);
    if (bytes.empty()) {
        logmsg(client_tag(to) + ": ERROR reply too long to frame (" +
               std::to_string(text.size()) + " bytes), not sent");
        return;
    }
    if (net_.send_to(to, bytes))
        logmsg(client_tag(to) + " SEND " + printable(text));
    else
        logmsg(client_tag(to) + ": reply dropped, client is gone: " + printable(text));
}
