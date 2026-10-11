#pragma once
#include <string>
#include <vector>
#include <iostream>
#include <cstddef>
#include <cstring>
#include <arpa/inet.h>


int extracframes(std::string& buf, std::vector<std::string>& out);

// Communication Protocol interface
namespace comproto {

    struct client {
        std::string name;
        uint64_t ID;
        
        // Application UID of client.
        std::string get_UID() const {return name + std::to_string(ID); } ;

        // enforce setting the name and ID upon instantiation of client identifier
        client(std::string n, double id) : name(n), ID(id) {}

    };
    namespace frame {
        const char SOH = 0x01;                 // start of header
        const char STX = 0x02;                 // start of text
        const char ETX = 0x03;                 // end of text
        const size_t MAX_PAYLOAD = 5000;       // max payload size
        const size_t FRAME_OVERHEAD = 5;       // SOH + 2-byte length + STX + ETX
        const size_t MAX_FRAME = MAX_PAYLOAD + FRAME_OVERHEAD;    // max total frame length, framing included
        
        std::vector<char> encode(const std::string& command);
        std::vector<char> decode(const std::string& command);
    }

    namespace command {

        inline const auto format = [](const auto& first, const auto&... rest) -> std::string {
            // Fold Expression: loops through 'rest' and appends "," + item to the first item
            std::string result = first;
            ((result += "," + rest), ...); 
            return result;
            };

        // -- Defined commands --
        struct sendmsg {
            const std::string command_name = "SENDMSG";
            client sender;
            client recipient;
            std::string message;

            sendmsg(client s, client r, std::string m) : sender(s), recipient(r), message(m) {}
            std::string to_string() const {
                return command::format(command_name, sender.get_UID(), recipient.get_UID(), message);
            }
        };

        struct getmsg {
            const std::string command_name = "GETMSG";
            client user;

            getmsg(client u) : user(u) {}
            std::string to_string() const {
                return command::format(command_name,user.get_UID());
            }
        };

        struct statusreq {
            const std::string command_name = "STATUSREQ";

            std::string to_string() const {
                return command::format(command_name);
            }
        };
    }
}

namespace Helper {
    unsigned int parse_port(const char* str, const char* label);
}


