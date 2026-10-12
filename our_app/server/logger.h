#pragma once
#include <fstream>
#include <string>

#include "../common.h"

// Writes one timestamped line to server.log:
//   [12/Oct/2026:18:23:45 +0000] <line>
// The timestamp is local time with its UTC offset (Common Log Format).
inline void logmsg(const std::string& line) {
    static std::ofstream out("server.log", std::ios::app);
    out << '[' << tool::log_timestamp() << "] " << line << std::endl;  // endl flushes
}
