#pragma once
#include <string>
#include <vector>
#include <iostream>
#include <cstddef>
#include <cstring>
#include <arpa/inet.h>
#include <unordered_map>
#include <cstdint>
#include <cerrno>
#include <cstdlib>




// Communication Protocol interface
namespace comproto {

    // Client-side helper that builds the user name that goes on the wire.
    // NOTE: messages store plain strings, because the wire carries no (name, ID) split.
    struct client {
        std::string name;
        uint64_t ID;

        client(std::string n, uint64_t id) : name(std::move(n)), ID(id) {}

        std::string get_UID() const { return name + std::to_string(ID); }
    };

    // ---------------------------------------------------------------------
    // base_com: abstract base of everything that travels inside a frame.
    // parse() is non-virtual: it splits the text and checks the field count,
    // then calls the derived class's own rule, parse_fields() (pure virtual).
    // ---------------------------------------------------------------------
    struct base_com {
        virtual ~base_com() = default;

        virtual const char* name() const = 0;       // wire name ("SENDMSG", ...)
        virtual std::string to_string() const = 0;  // payload text, no framing

        // Parse payload text into *this. false = not valid for this message type.
        bool parse(const std::string& payload);

    protected:
        // Number of comma-separated fields, command name included.
        // The LAST field absorbs any remaining commas.
        virtual size_t field_count() const = 0;

        // Derived type's own parsing rule. fields.size() == field_count() is guaranteed.
        virtual bool parse_fields(const std::vector<std::string>& fields) = 0;

        // ---- shared helpers for derived classes ----
        static std::vector<std::string> split(const std::string& s, size_t max_fields);
        bool has_name(const std::vector<std::string>& fields) const;
        static bool is_plain_token(const std::string& s);  // non-empty, no ','
    };

    // Free-form text from the server to the client (STATUSREQ list, "EMPTY",
    // GETMSG/SENDMSG replies...). Accepts any text.
    struct response : base_com {
        std::string text;

        response() = default;
        explicit response(std::string t) : text(std::move(t)) {}

        const char* name() const override { return "RESPONSE"; }
        std::string to_string() const override { return text; }

    protected:
        size_t field_count() const override { return 1; }
        bool parse_fields(const std::vector<std::string>& f) override {
            text = f[0];
            return true;
        }
    };

    namespace command {

        inline const auto format = [](const auto& first, const auto&... rest) -> std::string {
            std::string result = first;
            ((result += "," + rest), ...);
            return result;
        };

        // SENDMSG,<from>,<to>,<message>
        struct sendmsg : base_com {
            static constexpr const char* NAME = "SENDMSG";
            std::string sender;
            std::string recipient;
            std::string message;

            sendmsg() = default;  // needed by the decoder
            sendmsg(std::string s, std::string r, std::string m)
                : sender(std::move(s)), recipient(std::move(r)), message(std::move(m)) {}
            sendmsg(const client& s, const client& r, std::string m)
                : sender(s.get_UID()), recipient(r.get_UID()), message(std::move(m)) {}

            const char* name() const override { return NAME; }
            std::string to_string() const override {
                return format(std::string(NAME), sender, recipient, message);
            }

        protected:
            size_t field_count() const override { return 4; }
            bool parse_fields(const std::vector<std::string>& f) override {
                if (!has_name(f) || !is_plain_token(f[1]) || !is_plain_token(f[2])) return false;
                sender = f[1];
                recipient = f[2];
                message = f[3];  // may contain commas, may be empty
                return true;
            }
        };

        // GETMSG,<username>
        struct getmsg : base_com {
            static constexpr const char* NAME = "GETMSG";
            std::string user;

            getmsg() = default;
            explicit getmsg(std::string u) : user(std::move(u)) {}
            explicit getmsg(const client& u) : user(u.get_UID()) {}

            const char* name() const override { return NAME; }
            std::string to_string() const override { return format(std::string(NAME), user); }

        protected:
            size_t field_count() const override { return 2; }
            bool parse_fields(const std::vector<std::string>& f) override {
                if (!has_name(f) || !is_plain_token(f[1])) return false;
                user = f[1];
                return true;
            }
        };

        // STATUSREQ
        struct statusreq : base_com {
            static constexpr const char* NAME = "STATUSREQ";

            const char* name() const override { return NAME; }
            std::string to_string() const override { return NAME; }

        protected:
            size_t field_count() const override { return 1; }
            bool parse_fields(const std::vector<std::string>& f) override { return has_name(f); }
        };
    }

    namespace frame {
        const char SOH = 0x01;                 // start of header
        const char STX = 0x02;                 // start of text
        const char ETX = 0x03;                 // end of text
        const size_t MAX_PAYLOAD = 5000;       // max payload size
        const size_t FRAME_OVERHEAD = 5;       // SOH + 2-byte length + STX + ETX
        const size_t MAX_FRAME = MAX_PAYLOAD + FRAME_OVERHEAD;

        // to_server: payload must be a known command. to_client: any text is a response.
        enum class direction { to_server, to_client };

        // <SOH><len><STX> msg->to_string() <ETX>. Empty vector if null or too long.
        std::vector<char> encode(const base_com* msg);
        std::vector<char> encode_payload(const std::string& payload);

        // nullptr if unknown/invalid. Caller owns the result (delete it).
        base_com* decode(const std::string& payload, direction dir);

        // Pulls every COMPLETE frame out of `buf`, decodes it, appends to `out`.
        // Incomplete trailing bytes stay in `buf`. Returns the number of malformed/
        // over-long frames or undecodable payloads discarded. If `rejected` is given,
        // payloads of well-framed but undecodable frames are appended to it.
        // Caller owns every pointer in `out`.
        int extract_frames(std::string& buf,
                           std::vector<base_com*>& out,
                           direction dir,
                           std::vector<std::string>* rejected = nullptr);
    }
}

namespace tool {
    unsigned int parse_port(const char* str, const char* label);
}


