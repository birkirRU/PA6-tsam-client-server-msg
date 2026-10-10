#include <cstddef>
#include <string>
#include "common.h"


std::vector<char> encodeFRAME(const std::string& payload)
{
    if (payload.size() > MAX_PAYLOAD) {
        return {}; 
    }

    // Calculate total expected size
    uint16_t total_size = static_cast<uint16_t>(payload.size() + FRAME_OVERHEAD);

    std::vector<char> frame;
    frame.reserve(total_size);

    // Build the frame payload sequentially
    frame.push_back(SOH);
    
    // Network byte order
    frame.push_back(static_cast<char>((total_size >> 8) & 0xFF)); // Most Significant Byte (MSB)
    frame.push_back(static_cast<char>(total_size & 0xFF)); // Least Significant Byte (LSB)
    
    frame.push_back(STX);
    
    // Append the string contents into the vector
    frame.insert(frame.end(), payload.begin(), payload.end());
    
    frame.push_back(ETX);

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



