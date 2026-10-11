#include <cstddef>
#include <string>
#include "common.h"

std::vector<char> comproto::frame::encode(const std::string& command)
{
    if (command.size() > MAX_PAYLOAD) {
        return {}; 
    }

    // Calculate total expected size
    uint16_t total_size = static_cast<uint16_t>(command.size() + FRAME_OVERHEAD);

    std::vector<char> frame;
    frame.reserve(total_size);

    // Build the frame payload sequentially
    frame.push_back(SOH);
    
    // Network byte order
    frame.push_back(static_cast<char>((total_size >> 8) & 0xFF)); // Most Significant Byte (MSB)
    frame.push_back(static_cast<char>(total_size & 0xFF)); // Least Significant Byte (LSB)
    
    frame.push_back(STX);
    
    // Append the string contents into the vector
    frame.insert(frame.end(), command.begin(), command.end());
    
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

int extractFrames(std::string& buf, std::vector<std::string>& out){   //extracts a whole command and discards the ones that are not complete/correct
    int discarded = 0;
    while (!buf.empty()){   
        if (buf[0] != SOH){ 
            size_t next = buf.find(SOH); //find the start of header
            buf.erase(0, next == std::string::npos ? buf.size() : next); //dont understand these "::""
            discarded++;
            continue;
        }

        if (buf.size < 3) break; // length not here anymore (find out what this means) TODO
        size_t len = (static_cast<unsigned char>(buf[1]) << 8) | static_cast<unsigned char>(buf[2]); // explain this one aswell

        if (len < FRAME_OVERHEAD || len > MAX_FRAME || (buf.size() >= 4 && buf[3] != STX)) { // sizes that dont make sense
            buf.erase(0, 1); //drop this SOH and start looking for the next one
            discarded++; continue; 
        }
            
        if (buf.size() < len) break;

        if (buf[len - 1] != ETX) {  // ETX missing
            buf.erase(0, 1); 
            discarded++; 
            continue;
        }
        out.push_back(buf.substr(4, len - FRAME_OVERHEAD));  // the command between STX and ETX
        buf.erase(0, len);
    }
    return discarded;
}
