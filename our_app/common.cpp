#include "common.h"

#include <sys/socket.h>

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <unordered_map>

namespace comproto {

    // ---------------------------------------------------------------------
    // Usernames
    // ---------------------------------------------------------------------
    namespace {
        bool is_ascii_letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
        bool is_ascii_digit(char c)  { return c >= '0' && c <= '9'; }
    }

    bool is_valid_name(const std::string& s) {
        if (s.empty() || s.size() > MAX_NAME_LENGTH) return false;
        for (char c : s)
            if (!is_ascii_letter(c)) return false;
        return true;
    }

    bool is_valid_uid(const std::string& s) {
        size_t i = 0;
        while (i < s.size() && is_ascii_letter(s[i])) i++;
        if (i == 0 || i > MAX_NAME_LENGTH || i == s.size()) return false;  // needs letters, then digits
        for (size_t j = i; j < s.size(); j++)
            if (!is_ascii_digit(s[j])) return false;
        return s.size() - i <= 20;  // a uint64_t has at most 20 digits
    }

    std::string make_uid(const std::string& name, ClientID id) {
        return name + std::to_string(id);
    }

    // ---------------------------------------------------------------------
    // msg / base_com
    // ---------------------------------------------------------------------

    // "SENDMSG,a,b,hi,there" with max_fields=4 -> {"SENDMSG","a","b","hi,there"}
    std::vector<std::string> msg::split(const std::string& s, size_t max_fields) {
        std::vector<std::string> out;
        size_t start = 0;
        while (out.size() + 1 < max_fields) {            // keep room for the "rest" field
            size_t comma = s.find(',', start);
            if (comma == std::string::npos) break;
            out.push_back(s.substr(start, comma - start));
            start = comma + 1;
        }
        out.push_back(s.substr(start));                  // remainder, commas included
        return out;
    }

    bool base_com::parse(const std::string& payload) {
        std::vector<std::string> fields = split(payload, field_count());
        if (fields.size() != field_count()) return false;  // e.g. "SENDMSG,a,b" has too few
        if (fields[0] != name()) return false;
        return parse_fields(fields);
    }

    namespace command {

        bool sendmsg::parse_fields(const std::vector<std::string>& f) {
            if (!is_valid_uid(f[1]) || !is_valid_uid(f[2])) return false;
            sender = f[1];
            recipient = f[2];
            message = f[3];
            return true;
        }

        bool getmsg::parse_fields(const std::vector<std::string>& f) {
            if (!is_valid_uid(f[1])) return false;
            user = f[1];
            return true;
        }

        bool login::parse_fields(const std::vector<std::string>& f) {
            if (!is_valid_name(f[1])) return false;
            user_name = f[1];
            return true;
        }
    }

    // ---------------------------------------------------------------------
    // Replies
    // ---------------------------------------------------------------------
    namespace reply {

        namespace {
            const std::string EMPTY = "EMPTY";

            const std::string SENDMSG_OK        = "REP_SENDMSG: Message successfully sent to ";
            const std::string SENDMSG_NO_USER   = "REP_SENDMSG: Recipient doesn't exist";
            const std::string GETMSG_PREFIX     = "REP_GETMSG:";
            const std::string GETMSG_NONE       = "REP_GETMSG: No message";
            const std::string LOGIN_PREFIX      = "REP_LOGIN:";
            const std::string LOGIN_ALREADY     = "REP_LOGIN: Already logged in as ";

            bool starts_with(const std::string& s, const std::string& prefix) {
                return s.compare(0, prefix.size(), prefix) == 0;
            }

            // "a,b,c" -> {"a","b","c"}; every comma splits.
            std::vector<std::string> split_all(const std::string& s) {
                std::vector<std::string> out;
                size_t start = 0;
                for (size_t comma; (comma = s.find(',', start)) != std::string::npos; start = comma + 1)
                    out.push_back(s.substr(start, comma - start));
                out.push_back(s.substr(start));
                return out;
            }

            // Strict decimal: digits only, no sign, fits in size_t.
            bool parse_count(const std::string& s, size_t& out) {
                if (s.empty() || s.size() > 18) return false;
                out = 0;
                for (char c : s) {
                    if (c < '0' || c > '9') return false;
                    out = out * 10 + static_cast<size_t>(c - '0');
                }
                return true;
            }
        }

        std::string sendmsg::to_string() const {
            return delivered ? SENDMSG_OK + recipient : SENDMSG_NO_USER;
        }

        bool sendmsg::parse(const std::string& payload) {
            if (payload == SENDMSG_NO_USER) { delivered = false; recipient.clear(); return true; }
            if (!starts_with(payload, SENDMSG_OK)) return false;
            std::string r = payload.substr(SENDMSG_OK.size());
            if (!is_valid_uid(r)) return false;
            delivered = true;
            recipient = r;
            return true;
        }

        std::string getmsg::to_string() const {
            return has_message ? GETMSG_PREFIX + message : GETMSG_NONE;
        }

        // Note: a stored message whose text is exactly " No message" is indistinguishable
        // from the "no message" reply; this is accepted as a limitation of the format.
        bool getmsg::parse(const std::string& payload) {
            if (payload == GETMSG_NONE) { has_message = false; message.clear(); return true; }
            if (!starts_with(payload, GETMSG_PREFIX)) return false;
            has_message = true;
            message = payload.substr(GETMSG_PREFIX.size());
            return true;
        }

        std::string statusreq::to_string() const {
            if (counts.empty()) return EMPTY;
            std::string out;
            for (const auto& [uid, count] : counts) {
                if (!out.empty()) out += ',';
                out += uid + ":" + std::to_string(count);
            }
            return out;
        }

        bool statusreq::parse(const std::string& payload) {
            counts.clear();
            if (payload == EMPTY) return true;
            for (const std::string& entry : split_all(payload)) {
                size_t colon = entry.find(':');
                if (colon == std::string::npos) return false;
                std::string uid = entry.substr(0, colon);
                size_t count = 0;
                if (!is_valid_uid(uid) || !parse_count(entry.substr(colon + 1), count) || count == 0)
                    return false;
                counts.push_back({uid, count});
            }
            return true;
        }

        std::string listusers::to_string() const {
            if (users.empty()) return EMPTY;
            std::string out;
            for (const std::string& u : users) {
                if (!out.empty()) out += ',';
                out += u;
            }
            return out;
        }

        bool listusers::parse(const std::string& payload) {
            users.clear();
            if (payload == EMPTY) return true;
            for (const std::string& u : split_all(payload)) {
                if (!is_valid_uid(u)) return false;
                users.push_back(u);
            }
            return true;
        }

        std::string login::to_string() const {
            return accepted ? LOGIN_PREFIX + uid : LOGIN_ALREADY + uid;
        }

        bool login::parse(const std::string& payload) {
            std::string u;
            if (starts_with(payload, LOGIN_ALREADY)) {
                accepted = false;
                u = payload.substr(LOGIN_ALREADY.size());
            } else if (starts_with(payload, LOGIN_PREFIX)) {
                accepted = true;
                u = payload.substr(LOGIN_PREFIX.size());
            } else {
                return false;
            }
            if (!is_valid_uid(u)) return false;
            uid = u;
            return true;
        }
    }

    // ---------------------------------------------------------------------
    // Decoding
    // ---------------------------------------------------------------------
    namespace {
        template <typename T>
        using registry_t = std::unordered_map<std::string, std::unique_ptr<T> (*)()>;

        template <typename T, typename Base>
        std::unique_ptr<Base> make() { return std::make_unique<T>(); }

        // command name -> function that makes an empty command of that type
        const registry_t<base_com>& command_registry() {
            static const registry_t<base_com> r = {
                {command::sendmsg::NAME,   make<command::sendmsg, base_com>},
                {command::getmsg::NAME,    make<command::getmsg, base_com>},
                {command::statusreq::NAME, make<command::statusreq, base_com>},
                {command::listusers::NAME, make<command::listusers, base_com>},
                {command::login::NAME,     make<command::login, base_com>},
            };
            return r;
        }

        // command name -> function that makes an empty reply to that command
        const registry_t<response>& reply_registry() {
            static const registry_t<response> r = {
                {command::sendmsg::NAME,   make<reply::sendmsg, response>},
                {command::getmsg::NAME,    make<reply::getmsg, response>},
                {command::statusreq::NAME, make<reply::statusreq, response>},
                {command::listusers::NAME, make<reply::listusers, response>},
                {command::login::NAME,     make<reply::login, response>},
            };
            return r;
        }
    }

    std::unique_ptr<base_com> decode_command(const std::string& payload) {
        // Command name = everything before the first comma (or the whole payload).
        auto it = command_registry().find(payload.substr(0, payload.find(',')));
        if (it == command_registry().end()) return nullptr;  // unknown command

        std::unique_ptr<base_com> cmd = it->second();
        if (!cmd->parse(payload)) return nullptr;            // malformed
        return cmd;
    }

    std::unique_ptr<response> decode_reply(const std::string& payload, const std::string& sent_command) {
        // The server answers anything it could not understand with the same fixed text.
        if (payload == reply::UNKNOWN_COMMAND) return std::make_unique<reply::error>();

        auto it = reply_registry().find(sent_command);
        if (it == reply_registry().end()) return nullptr;

        std::unique_ptr<response> rep = it->second();
        if (!rep->parse(payload)) return nullptr;
        return rep;
    }

    // ---------------------------------------------------------------------
    // frame
    // ---------------------------------------------------------------------
    namespace frame {

        std::vector<char> encode(const msg* m) {
            if (m == nullptr) return {};
            return encode_payload(m->to_string());  // virtual call picks the right to_string()
        }

        std::vector<char> encode_payload(const std::string& payload) {
            if (payload.size() > MAX_PAYLOAD) return {};

            uint16_t total_size = static_cast<uint16_t>(payload.size() + FRAME_OVERHEAD);

            std::vector<char> frame;
            frame.reserve(total_size);

            frame.push_back(SOH);
            frame.push_back(static_cast<char>((total_size >> 8) & 0xFF)); // MSB (network order)
            frame.push_back(static_cast<char>(total_size & 0xFF));        // LSB
            frame.push_back(STX);
            frame.insert(frame.end(), payload.begin(), payload.end());
            frame.push_back(ETX);

            return frame;
        }

        int extract_frames(std::string& buf,
                           std::vector<std::string>& payloads,
                           std::vector<std::string>* problems) {
            int discarded = 0;
            auto discard = [&](size_t n, const std::string& why) {
                buf.erase(0, n);
                discarded++;
                if (problems != nullptr) problems->push_back(why);
            };

            while (!buf.empty()) {
                // Must start with SOH; otherwise skip to the next SOH (or drop everything).
                if (buf[0] != SOH) {
                    size_t next = buf.find(SOH);
                    size_t n = (next == std::string::npos) ? buf.size() : next;
                    discard(n, "garbage outside a frame (" + std::to_string(n) + " bytes)");
                    continue;
                }

                // Need SOH + both length bytes before the frame length is known.
                if (buf.size() < 3) break;

                // Length: 2 bytes, big-endian (high byte first).
                size_t len = (static_cast<unsigned char>(buf[1]) << 8) | static_cast<unsigned char>(buf[2]);

                if (len < FRAME_OVERHEAD) {
                    discard(1, "bad frame length " + std::to_string(len) + " (minimum is 5)");
                    continue;
                }
                if (len > MAX_FRAME) {
                    discard(1, "over-long frame: length " + std::to_string(len) +
                               " (maximum is " + std::to_string(MAX_FRAME) + ")");
                    continue;
                }
                if (buf.size() >= 4 && buf[3] != STX) {
                    discard(1, "missing STX after frame header");
                    continue;
                }

                // Header fine, rest of the frame not here yet.
                if (buf.size() < len) break;

                // Whole frame is here: it must end with ETX.
                if (buf[len - 1] != ETX) {
                    discard(1, "missing ETX at end of frame (length " + std::to_string(len) + ")");
                    continue;
                }

                // Good frame: payload sits between STX and ETX.
                payloads.push_back(buf.substr(4, len - FRAME_OVERHEAD));
                buf.erase(0, len);
            }
            return discarded;
        }
    }
}

namespace tool {

    unsigned int parse_port(const char* str, const char* label) {
        // strtoul(3): converts string to unsigned long;
        // errors via errno and endptr.
        char* endptr;
        errno = 0;
        unsigned long val = std::strtoul(str, &endptr, 10);

        // endptr == str: no digits were parsed at all (e.g. "abc").
        // *endptr != '\0': trailing garbage after the number (e.g. "80abc").
        if (endptr == str || *endptr != '\0') {
            std::cerr << label << " is not a valid unsigned integer: " << str << std::endl;
            exit(1);
        }
        // ERANGE: value too large to fit in unsigned long.
        if (errno == ERANGE || val > 65535) {
            std::cerr << label << " is out of valid port range (0-65535): " << str << std::endl;
            exit(1);
        }

        return (unsigned int) val;
    }
}

bool sendAll(int fd, const std::vector<char>& data)
{
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;   // interrupted by a signal, retry
            return false;                   // connection broken
        }
        sent += n;
    }
    return true;
}
