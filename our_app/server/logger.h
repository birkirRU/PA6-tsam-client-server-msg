#pragma once
#include <ctime>
#include <fstream>
#include <string>

// Writes one timestamped line to server.log.
inline void logmsg(const std::string& line) {
    static std::ofstream out("server.log", std::ios::app);
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char ts[32];
    std::strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);
    out << ts << ' ' << line << std::endl;  // endl flushes
}