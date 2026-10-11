#pragma once
#include <string>
#include <vector>
#include <iostream>
#include <cstddef>
#include <cstring>
#include <arpa/inet.h>


// Communication Protocol interface
namespace comproto {

    struct client {
        std::string name;
        uint64_t ID;
        
        // universal application UID of client.
        std::string get_UID() {return name + std::to_string(ID); } ;

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
        struct sendmsg {
            const std::string command_name = "SENDMSG";
            client sender;
            client recipient;
            std::string message;
        };

        struct getmsg {
            const std::string command_name = "GETMSG";
            client user;
        };


        struct statusreq {
            const std::string command_name = "STATUSREQ";
        };
    }
}

namespace Helper {
    unsigned int parse_port(const char* str, const char* label);
}



