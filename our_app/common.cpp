#pragma once
#include <cstddef>
#include <string>
#include <stdexcept>
#include "common.h"

const char SOH = 0x01;
const char STX = 0x02;
const char ETX = 0x03;

const size_t FRAME_OVERHEAD = 5; // SOH + 2-byte length + STX + ETX
const size_t MAX_PAYLOAD = 5000;

const char* const PROTOCOL_VERSION = "T-409-TSAM protocol version 3.2";

std::string encodeFRAME(const std::string& payload)
{
    if (payload.size() > MAX_FRAME_SIZE)
    
        throw std::runtime_error("Payload too large to encode in FRAME");
    
    
    
    
    
    
    }


namespace Helper {

    unsigned int parse_port(const char* str, const char* label) {
        // strtoul(3): converts string to unsigned long; 
        // errors via errno and endptr.
        char* endptr;
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



