#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Communication Protocol interface, shared by tsamserver and tsamclient.
//
// Everything that travels inside a frame is a `msg`. There are two families:
//
//   msg                      to_string() / parse(): object <-> payload text
//    ├─ base_com             a command sent by a client ("NAME,field,...")
//    │    └─ command::sendmsg / getmsg / statusreq / listusers / login
//    └─ response             a reply sent by the server; one subclass per command
//         └─ reply::sendmsg / getmsg / statusreq / listusers / login / error
//
// Replies carry no command name on the wire, so a reply can only be parsed by
// someone who knows which command it answers (see decode_reply()).
namespace comproto {

    // Identifies one TCP connection on the server. Assigned when the connection is accepted.
    using ClientID = uint64_t;

    // ---------------------------------------------------------------------
    // Usernames
    //
    // A user picks a name with LOGIN,<name>; the server answers with the user's
    // UID = <name><ClientID>, e.g. "bob" on connection 3 becomes "bob3".
    // All other commands identify users by UID.
    // ---------------------------------------------------------------------
    const size_t MAX_NAME_LENGTH = 32;

    bool is_valid_name(const std::string& s);  // 1-32 ASCII letters
    bool is_valid_uid(const std::string& s);   // ASCII letters followed by decimal digits
    std::string make_uid(const std::string& name, ClientID id);

    // ---------------------------------------------------------------------
    // msg: root of everything that travels inside a frame.
    // ---------------------------------------------------------------------
    struct msg {
        virtual ~msg() = default;

        // Payload text, without framing.
        virtual std::string to_string() const = 0;

        // Parse payload text into *this. false = not valid for this message type.
        virtual bool parse(const std::string& payload) = 0;

    protected:
        // "a,b,c,d" with max_fields=3 -> {"a","b","c,d"}: the last field keeps any remaining commas.
        static std::vector<std::string> split(const std::string& s, size_t max_fields);
    };

    // ---------------------------------------------------------------------
    // base_com: a command, i.e. "<NAME>,<field>,...".
    // parse() splits the text, checks the field count and the name, then hands
    // the fields to the derived class's own rule, parse_fields().
    // ---------------------------------------------------------------------
    struct base_com : msg {
        // Wire name ("SENDMSG", ...).
        virtual const char* name() const = 0;

        bool parse(const std::string& payload) override;

    protected:
        // Number of comma-separated fields, command name included.
        // The LAST field absorbs any remaining commas.
        virtual size_t field_count() const = 0;

        // Derived type's own rule. fields.size() == field_count() and fields[0] == name() are guaranteed.
        virtual bool parse_fields(const std::vector<std::string>& fields) = 0;
    };

    namespace command {

        // Joins its arguments with commas: format("GETMSG", "bob3") -> "GETMSG,bob3".
        inline const auto format = [](const auto& first, const auto&... rest) -> std::string {
            std::string result = first;
            ((result += "," + rest), ...);
            return result;
        };

        // SENDMSG,<from>,<to>,<message>   store <message> for user <to>
        struct sendmsg : base_com {
            static constexpr const char* NAME = "SENDMSG";
            std::string sender;     // UID
            std::string recipient;  // UID
            std::string message;    // any text, may contain commas, may be empty

            sendmsg() = default;
            sendmsg(std::string s, std::string r, std::string m)
                : sender(std::move(s)), recipient(std::move(r)), message(std::move(m)) {}

            const char* name() const override { return NAME; }
            std::string to_string() const override { return format(std::string(NAME), sender, recipient, message); }

        protected:
            size_t field_count() const override { return 4; }
            bool parse_fields(const std::vector<std::string>& f) override;
        };

        // GETMSG,<uid>   fetch (and delete) the oldest message stored for <uid>
        struct getmsg : base_com {
            static constexpr const char* NAME = "GETMSG";
            std::string user;  // UID

            getmsg() = default;
            explicit getmsg(std::string u) : user(std::move(u)) {}

            const char* name() const override { return NAME; }
            std::string to_string() const override { return format(std::string(NAME), user); }

        protected:
            size_t field_count() const override { return 2; }
            bool parse_fields(const std::vector<std::string>& f) override;
        };

        // STATUSREQ   which users have stored messages, and how many
        struct statusreq : base_com {
            static constexpr const char* NAME = "STATUSREQ";

            const char* name() const override { return NAME; }
            std::string to_string() const override { return NAME; }

        protected:
            size_t field_count() const override { return 1; }
            bool parse_fields(const std::vector<std::string>&) override { return true; }
        };

        // LISTUSERS   UIDs of all users currently logged in (extension)
        struct listusers : base_com {
            static constexpr const char* NAME = "LISTUSERS";

            const char* name() const override { return NAME; }
            std::string to_string() const override { return NAME; }

        protected:
            size_t field_count() const override { return 1; }
            bool parse_fields(const std::vector<std::string>&) override { return true; }
        };

        // LOGIN,<name>   register this connection as <name>; the reply carries the UID (extension)
        struct login : base_com {
            static constexpr const char* NAME = "LOGIN";
            std::string user_name;

            login() = default;
            explicit login(std::string n) : user_name(std::move(n)) {}

            const char* name() const override { return NAME; }
            std::string to_string() const override { return format(std::string(NAME), user_name); }

        protected:
            size_t field_count() const override { return 2; }
            bool parse_fields(const std::vector<std::string>& f) override;
        };
    }

    // ---------------------------------------------------------------------
    // response: a reply from the server. Each command has its own reply type
    // with its own wire format and parsing rule.
    // ---------------------------------------------------------------------
    struct response : msg {
        // Name of the command this reply answers ("" for reply::error).
        virtual const char* command() const = 0;
    };

    namespace reply {

        // Sent for any payload the server could not decode into a known, well-formed command.
        const char* const UNKNOWN_COMMAND = "command does not exist";

        // "REP_SENDMSG: Message successfully sent to <to>" | "REP_SENDMSG: Recipient doesn't exist"
        struct sendmsg : response {
            bool delivered = false;
            std::string recipient;  // set when delivered

            sendmsg() = default;
            sendmsg(bool d, std::string r) : delivered(d), recipient(std::move(r)) {}

            const char* command() const override { return command::sendmsg::NAME; }
            std::string to_string() const override;
            bool parse(const std::string& payload) override;
        };

        // "REP_GETMSG:<message>" | "REP_GETMSG: No message"
        struct getmsg : response {
            bool has_message = false;
            std::string message;  // set when has_message

            getmsg() = default;
            explicit getmsg(std::string m) : has_message(true), message(std::move(m)) {}

            const char* command() const override { return command::getmsg::NAME; }
            std::string to_string() const override;
            bool parse(const std::string& payload) override;
        };

        // "<uid1>:<count1>,<uid2>:<count2>,..." | "EMPTY"   (format fixed by the assignment)
        struct statusreq : response {
            std::vector<std::pair<std::string, size_t>> counts;  // only users with >= 1 message

            const char* command() const override { return command::statusreq::NAME; }
            std::string to_string() const override;
            bool parse(const std::string& payload) override;
        };

        // "<uid1>,<uid2>,..." | "EMPTY"
        struct listusers : response {
            std::vector<std::string> users;

            const char* command() const override { return command::listusers::NAME; }
            std::string to_string() const override;
            bool parse(const std::string& payload) override;
        };

        // "REP_LOGIN:<uid>" | "REP_LOGIN: Already logged in as <uid>"
        struct login : response {
            bool accepted = false;
            std::string uid;  // the new UID, or the one this connection already has

            login() = default;
            login(bool a, std::string u) : accepted(a), uid(std::move(u)) {}

            const char* command() const override { return command::login::NAME; }
            std::string to_string() const override;
            bool parse(const std::string& payload) override;
        };

        // UNKNOWN_COMMAND
        struct error : response {
            const char* command() const override { return ""; }
            std::string to_string() const override { return UNKNOWN_COMMAND; }
            bool parse(const std::string& payload) override { return payload == UNKNOWN_COMMAND; }
        };
    }

    // Payload -> command. nullptr if the command is unknown or malformed.
    std::unique_ptr<base_com> decode_command(const std::string& payload);

    // Payload -> reply to the command named `sent_command` (the name of the command
    // this reply answers; replies arrive in the order the commands were sent).
    // nullptr if the payload is not a valid reply to that command.
    std::unique_ptr<response> decode_reply(const std::string& payload, const std::string& sent_command);

    namespace frame {
        const char SOH = 0x01;                 // start of header
        const char STX = 0x02;                 // start of text
        const char ETX = 0x03;                 // end of text
        const size_t MAX_PAYLOAD = 5000;       // max command length (between STX and ETX)
        const size_t FRAME_OVERHEAD = 5;       // SOH + 2-byte length + STX + ETX
        const size_t MAX_FRAME = MAX_PAYLOAD + FRAME_OVERHEAD;

        // <SOH><len><STX> msg->to_string() <ETX>. Empty vector if null or too long.
        std::vector<char> encode(const msg* m);
        std::vector<char> encode_payload(const std::string& payload);

        // Removes every COMPLETE frame from the front of `buf` and appends its payload to
        // `payloads`. An incomplete trailing frame stays in `buf`. Returns the number of
        // discarded byte runs; if `problems` is given, one description per discard is appended.
        int extract_frames(std::string& buf,
                           std::vector<std::string>& payloads,
                           std::vector<std::string>* problems = nullptr);
    }
}

namespace tool {
    // Parses a TCP port (0-65535). Prints an error and exits on invalid input.
    unsigned int parse_port(const char* str, const char* label);
}

// Sends every byte of data, retrying partial sends. false = connection broken.
bool sendAll(int fd, const std::vector<char>& data);
