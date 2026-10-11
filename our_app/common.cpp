#include <cstddef>
#include <string>
#include "common.h"



namespace comproto {

    // ---------------------------------------------------------------------
    // base_com
    // ---------------------------------------------------------------------
    bool base_com::parse(const std::string& payload) {
        std::vector<std::string> fields = split(payload, field_count());
        if (fields.size() != field_count()) return false;  // e.g. "SENDMSG,a,b" has too few
        return parse_fields(fields);
    }

    // "SENDMSG,a,b,hi,there" with max_fields=4 -> {"SENDMSG","a","b","hi,there"}
    std::vector<std::string> base_com::split(const std::string& s, size_t max_fields) {
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

    bool base_com::has_name(const std::vector<std::string>& fields) const {
        return !fields.empty() && fields[0] == name();
    }

    bool base_com::is_plain_token(const std::string& s) {
        return !s.empty() && s.find(',') == std::string::npos;
    }

    // ---------------------------------------------------------------------
    // frame
    // ---------------------------------------------------------------------
    namespace frame {

        std::vector<char> encode(const base_com* msg) {
            if (msg == nullptr) return {};
            return encode_payload(msg->to_string());  // virtual call picks the right to_string()
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

        namespace {
            using creator = base_com* (*)();

            // command name -> function that makes an empty message of that type
            const std::unordered_map<std::string, creator>& registry() {
                static const std::unordered_map<std::string, creator> r = {
                    {command::sendmsg::NAME,   []() -> base_com* { return new command::sendmsg(); }},
                    {command::getmsg::NAME,    []() -> base_com* { return new command::getmsg(); }},
                    {command::statusreq::NAME, []() -> base_com* { return new command::statusreq(); }},
                };
                return r;
            }
        }

        base_com* decode(const std::string& payload, direction dir) {
            base_com* msg = nullptr;

            if (dir == direction::to_client) {
                msg = new response();
            } else {
                // Command name = everything before the first comma (or the whole payload).
                const std::string cmd = payload.substr(0, payload.find(','));
                auto it = registry().find(cmd);
                if (it == registry().end()) return nullptr;  // unknown command
                msg = it->second();
            }

            if (!msg->parse(payload)) {  // wrong field count or derived rule rejected it
                delete msg;
                return nullptr;
            }
            return msg;
        }

        int extract_frames(std::string& buf,
                           std::vector<base_com*>& out,
                           direction dir,
                           std::vector<std::string>* rejected) {
            int discarded = 0;
            while (!buf.empty()) {
                // Must start with SOH; otherwise skip to the next SOH (or drop everything).
                if (buf[0] != SOH) {
                    size_t next = buf.find(SOH);
                    buf.erase(0, next == std::string::npos ? buf.size() : next);
                    discarded++;
                    continue;
                }

                // Need SOH + both length bytes before the frame length is known.
                if (buf.size() < 3) break;

                // Length: 2 bytes, big-endian (high byte first).
                size_t len = (static_cast<unsigned char>(buf[1]) << 8) | static_cast<unsigned char>(buf[2]);

                // Implausible length, or the 4th byte is here and is not STX.
                if (len < FRAME_OVERHEAD || len > MAX_FRAME || (buf.size() >= 4 && buf[3] != STX)) {
                    buf.erase(0, 1);  // drop this SOH, look for the next one
                    discarded++;
                    continue;
                }

                // Header fine, rest of the frame not here yet.
                if (buf.size() < len) break;

                // Whole frame is here: it must end with ETX.
                if (buf[len - 1] != ETX) {
                    buf.erase(0, 1);
                    discarded++;
                    continue;
                }

                // Good frame: payload sits between STX and ETX.
                std::string payload = buf.substr(4, len - FRAME_OVERHEAD);
                buf.erase(0, len);

                base_com* msg = decode(payload, dir);
                if (msg != nullptr) {
                    out.push_back(msg);
                } else {
                    discarded++;
                    if (rejected != nullptr) rejected->push_back(payload);
                }
            }
            return discarded;
        }
    }
}

namespace Helper {

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
