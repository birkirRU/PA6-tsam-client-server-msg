#include <cstddef>
#include <string>
#include "common.h"

std::string encodeFRAME(const std::string& payload)
{ 
    if (payload.size() > MAX_PAYLOAD)
        return "";
    size_t total = payload.size() + FRAME_OVERHEAD;

    std::string frame;
    frame.reserve(total);
    frame += SOH;
    frame += static_cast<char>((total >> 8) & 0xFF);
    frame += static_cast<char>(total & 0xFF);
    frame += STX;
    frame += payload;
    frame += ETX;
    return frame;
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



