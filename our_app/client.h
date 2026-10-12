#pragma once
#include <deque>
#include <string>

// tsamclient: reads commands from stdin, sends them framed to the server and
// prints every command sent and every reply received with a timestamp.
class Client {
public:
    // Connects to host:port. Exits the program with an error message on failure.
    Client(const char* host, const char* port);
    ~Client();

    // Event loop: waits on stdin and the server socket at the same time.
    // Returns the process exit code.
    int run();

private:
    void on_stdin_readable();
    void on_socket_readable();
    void send_line(const std::string& line);
    void handle_reply(const std::string& payload);
    void print(const char* tag, const std::string& text);

    int sock_ = -1;
    bool running_ = true;
    bool stdin_open_ = true;
    std::string input_;              // typed text not yet ended by a newline
    std::string recv_buf_;           // server bytes not yet forming a full frame
    std::deque<std::string> sent_;   // names of commands still waiting for a reply, oldest first
    std::string uid_;                // our UID once LOGIN has succeeded
};
