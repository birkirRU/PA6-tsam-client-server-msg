#pragma once
#include <string>
#include <iostream>
#include <cstddef>

const char SOH = 0x01;                 // start of header
const char STX = 0x02;                 // start of text
const char ETX = 0x03;                 // end of text
const size_t MAX_PAYLOAD = 5000;       // max payload size
const size_t FRAME_OVERHEAD = 5;       // SOH + 2-byte length + STX + ETX
const size_t MAX_FRAME = MAX_PAYLOAD + FRAME_OVERHEAD;    // max total frame length, framing included

std::string encodeFRAME(const std::string& payload);
namespace Helper {
    unsigned int parse_port(const char* str, const char* label);
}



