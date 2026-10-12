#include "client.h"

#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "common.h"

namespace {
    // After stdin closes, wait at most this long for outstanding replies.
    const int REPLY_TIMEOUT_MS = 5000;
}

Client::Client(const char* host, const char* port) {
    struct addrinfo hints{};
    hints.ai_family = AF_INET;        // IPv4
    hints.ai_socktype = SOCK_STREAM;  // TCP

    struct addrinfo* res = nullptr;
    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        exit(1);
    }

    sock_ = socket(res->ai_family, res->ai_socktype, 0);
    if (sock_ < 0) {
        perror("socket");
        freeaddrinfo(res);
        exit(1);
    }

    if (connect(sock_, res->ai_addr, res->ai_addrlen) < 0) {
        perror("connect");
        close(sock_);
        freeaddrinfo(res);
        exit(1);
    }

    freeaddrinfo(res);
    print("INFO", std::string("connected to ") + host + ":" + port);
    print("INFO", "commands: LOGIN,<name>  SENDMSG,<from>,<to>,<message>  GETMSG,<uid>  STATUSREQ  LISTUSERS");
}

Client::~Client() {
    if (sock_ >= 0) close(sock_);
}

int Client::run() {
    struct pollfd fds[2];
    fds[0].fd = STDIN_FILENO;  // the user typing
    fds[0].events = POLLIN;
    fds[1].fd = sock_;         // the server
    fds[1].events = POLLIN;

    while (running_) {
        // Once stdin is closed we only wait for the replies still owed to us.
        if (!stdin_open_ && sent_.empty()) break;
        fds[0].fd = stdin_open_ ? STDIN_FILENO : -1;  // poll ignores negative fds
        int timeout = stdin_open_ ? -1 : REPLY_TIMEOUT_MS;

        int n = poll(fds, 2, timeout);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            return 1;
        }
        if (n == 0) {
            print("INFO", "no reply to " + std::to_string(sent_.size()) + " command(s), giving up");
            break;
        }

        if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) on_stdin_readable();
        if (running_ && (fds[1].revents & (POLLIN | POLLHUP | POLLERR))) on_socket_readable();
    }
    return 0;
}

// Reads whatever the user typed and sends every complete line.
// read() on the fd (not fgets) so nothing hides in a stdio buffer that poll() cannot see.
void Client::on_stdin_readable() {
    char tmp[1024];
    ssize_t r = read(STDIN_FILENO, tmp, sizeof tmp);
    if (r < 0 && errno == EINTR) return;
    if (r <= 0) {                        // 0 = Ctrl-D / end of piped input
        if (!input_.empty()) send_line(input_);  // last line without a newline
        input_.clear();
        stdin_open_ = false;
        return;
    }
    input_.append(tmp, r);

    size_t nl;
    while (running_ && (nl = input_.find('\n')) != std::string::npos) {
        std::string line = input_.substr(0, nl);
        input_.erase(0, nl + 1);
        send_line(line);
    }
}

void Client::send_line(const std::string& raw) {
    std::string line = raw;
    if (!line.empty() && line.back() == '\r') line.pop_back();  // typed on Windows / via telnet
    if (line.empty()) return;

    std::vector<char> frame = comproto::frame::encode_payload(line);
    if (frame.empty()) {
        print("ERROR", "command too long (max " + std::to_string(comproto::frame::MAX_PAYLOAD) +
                       " bytes), not sent");
        return;
    }
    if (!sendAll(sock_, frame)) {
        perror("send");
        running_ = false;
        return;
    }
    print("SENT", line);
    sent_.push_back(line.substr(0, line.find(',')));  // replies come back in this order
}

void Client::on_socket_readable() {
    char tmp[4096];
    ssize_t r = recv(sock_, tmp, sizeof tmp, 0);
    if (r == 0) {
        print("INFO", "server closed the connection");
        running_ = false;
        return;
    }
    if (r < 0) {
        if (errno == EINTR) return;
        perror("recv");
        running_ = false;
        return;
    }
    recv_buf_.append(tmp, r);

    std::vector<std::string> payloads;
    std::vector<std::string> problems;
    comproto::frame::extract_frames(recv_buf_, payloads, &problems);
    for (const std::string& why : problems) print("WARNING", "discarded bytes from server: " + why);
    for (const std::string& p : payloads) handle_reply(p);
}

// Matches a reply to the oldest command still waiting for one.
void Client::handle_reply(const std::string& payload) {
    print("RECV", payload);

    if (sent_.empty()) {
        print("WARNING", "reply received but no command was waiting for one");
        return;
    }
    std::string command = sent_.front();
    sent_.pop_front();

    std::unique_ptr<comproto::response> rep = comproto::decode_reply(payload, command);
    if (rep == nullptr) {
        print("WARNING", "not a valid reply to " + command);
        return;
    }
    if (auto* login = dynamic_cast<comproto::reply::login*>(rep.get())) {
        uid_ = login->uid;
        print("INFO", "your user id is " + uid_);
    }
}

void Client::print(const char* tag, const std::string& text) {
    printf("%s %s: %s\n", tool::display_timestamp().c_str(), tag, text.c_str());
}

int main(int argc, char* argv[]) {
    signal(SIGPIPE, SIG_IGN);  // a dead server makes send() return -1 instead of killing us

    // Line-buffer stdout even when it is redirected to a file (client-output.txt),
    // so every line is written as soon as it is printed.
    setvbuf(stdout, nullptr, _IOLBF, 0);

    if (argc != 3) {
        fprintf(stderr, "usage: %s <ip> <port>\n", argv[0]);
        return 1;
    }
    tool::parse_port(argv[2], "port");

    Client client(argv[1], argv[2]);
    return client.run();
}
