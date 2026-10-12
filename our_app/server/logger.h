#pragma once
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#include "../common.h"

// The server log, server.log in the working directory. One line per event:
//   [12/Oct/2026:18:23:45 +0000] <line>
// The timestamp is local time with its UTC offset (Common Log Format).
namespace serverlog {

    inline std::ofstream& stream() {
        static std::ofstream out;
        return out;
    }

    // Opens the log for appending. false (with errno set) if it cannot be opened.
    inline bool open(const char* path) {
        stream().open(path, std::ios::app);
        return stream().is_open();
    }
}

// Writes one line to the log. A failing write (e.g. disk full) never stops the
// server: it is reported once on stderr and later lines are attempted again.
inline void logmsg(const std::string& line) {
    static bool reported = false;
    std::ofstream& out = serverlog::stream();
    out << '[' << tool::log_timestamp() << "] " << line << std::endl;  // endl flushes
    if (!out) {
        if (!reported) fprintf(stderr, "warning: writing server.log failed, log lines are being lost\n");
        reported = true;
        out.clear();  // try again on the next line
    }
}

// errno as text, for log lines: "accept: Too many open files".
inline std::string errstr(const char* what, int err) {
    return std::string(what) + ": " + std::strerror(err);
}

// Fatal startup/runtime error: reported on stderr and in the log, then exit(1).
[[noreturn]] inline void fatal(const std::string& why) {
    fprintf(stderr, "tsamserver: %s\n", why.c_str());
    logmsg("FATAL " + why);
    exit(1);
}
