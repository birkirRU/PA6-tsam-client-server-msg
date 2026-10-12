# Assignment 6: Client-Server Messaging

A mail server (`tsamserver`) and a client (`tsamclient`) that talk over TCP using
the framed protocol from the assignment.

## Compiling and running

```bash
make                    # builds tsamserver and tsamclient (plus *_debug variants with -g)
./tsamserver 4044       # listen on port 4044 (any port 1-65535)
./tsamclient 127.0.0.1 4044
```

`make tsamserver` / `make tsamclient` build one program; `make clean` removes the binaries.
Requires `g++` with C++20 support and `make`. No third-party libraries are used.

The server writes its log to `server.log` in the directory it is started from.
The file is emptied at every start, so each run produces the log of one session.
Stop the server with Ctrl-C (or `kill`); it then writes a final shutdown line.

The client reads commands from stdin, one per line, and prints to stdout. To save a
session: `./tsamclient 127.0.0.1 4044 | tee client-output.txt`.

## Operating system

- **Server:** Linux only. It uses `epoll`, `accept4` and `SOCK_NONBLOCK`.
  It must be built and tested on Linux, such as the TSAM server (Ubuntu 24.04).
- **Client:** portable POSIX (uses `poll`). It was developed on macOS 27 and runs on Linux.

## Protocol

Every command and every reply is sent as one frame:

```
<SOH><length><STX><command><ETX>
```

`SOH`, `STX` and `ETX` are the bytes 0x01, 0x02 and 0x03. `length` is a 16-bit
unsigned integer in network byte order (big-endian). It counts the whole frame,
including the 5 framing bytes. The command between STX and ETX may be at most
**5000 bytes**, so a frame is at most 5005 bytes.

### Users and UIDs

A user picks a name with `LOGIN`. The server combines it with the number of the
connection to form the user's **UID**. For example, `bob` logging in on connection 3
becomes `bob3`. All other commands identify users by UID.

- A name is 1-32 ASCII letters (`A-Z`, `a-z`); no digits, spaces or punctuation.
- A UID is a name followed by decimal digits.
- Each connection can log in once. A user who reconnects gets a new UID, and
  messages for the old UID can still be fetched with `GETMSG,<old uid>`.

### Commands

| Command | Meaning |
|---|---|
| `LOGIN,<name>` | Log this connection in as `<name>` (extension) |
| `LISTUSERS` | List the UIDs of all users currently logged in (extension) |
| `SENDMSG,<from>,<to>,<message>` | Store `<message>` for user `<to>`, sent by `<from>` |
| `GETMSG,<uid>` | Return the oldest message stored for `<uid>` and delete it |
| `STATUSREQ` | List which users have stored messages, and how many |

`<from>`, `<to>` and `<uid>` must be valid UIDs. The message may contain commas and
may be empty. Command names are case-sensitive.

### Replies

Replies do not repeat the command name. A client knows which command a reply answers
because the server replies to the commands on one connection in the order they were sent.

| Command | Reply |
|---|---|
| `LOGIN` | `REP_LOGIN:<uid>`, or `REP_LOGIN: Already logged in as <uid>` |
| `LISTUSERS` | `<uid1>,<uid2>,...` (sorted), or `EMPTY` if nobody is logged in |
| `SENDMSG` | `REP_SENDMSG: Message successfully sent to <to>`, or `REP_SENDMSG: Recipient doesn't exist` |
| `GETMSG` | `REP_GETMSG:<message>`, or `REP_GETMSG: No message` |
| `STATUSREQ` | `<uid1>:<count1>,<uid2>:<count2>,...`, or `EMPTY` (format fixed by the assignment) |
| unknown or malformed command | `command does not exist` |

## Server behaviour

- **SENDMSG** stores the message only if `<to>` is a UID that has logged in at some
  point since the server started. The recipient does not have to be online. Any other
  `<to>` gets `Recipient doesn't exist` and nothing is stored. `<from>` is not checked
  against the sender's own UID.
- **GETMSG** returns and deletes the oldest message for that UID. Anyone may fetch
  messages for any UID. The reply contains only the message text, not the sender.
- **STATUSREQ** lists only users with at least one stored message, in alphabetical
  order of UID.
- **LISTUSERS** lists users that are logged in right now. A user disappears from the
  list when their connection closes.
- **Unknown or malformed commands** are discarded, and the server answers
  `command does not exist`. This covers an unknown name, a wrong number of fields, an
  invalid name or UID, and the wrong case (`statusreq`).
- **Malformed frames** (bytes outside a frame, a length below 5 or above 5005, a missing
  STX or ETX) are discarded without a reply. The server then looks for the next SOH
  and keeps serving that client. Every discard is logged with its reason.
- **Partial and joined frames.** TCP may split a frame across reads or join several into
  one. The server keeps a buffer per client and only processes complete frames.
- **Disconnects.** Commands a client sent just before disconnecting are still processed.
  The client is then logged out. Its stored messages stay. An incomplete frame left
  behind is logged and dropped.
- **Many clients.** A single thread serves all clients with `epoll`. It sleeps until a
  socket is ready, so CPU use is zero when idle. Replies that do not fit in the socket
  buffer are queued and sent when the client reads, without blocking other clients.
- **Errors.**
  - Startup failures are logged and the server exits with status 1: port in use,
    `server.log` cannot be opened, socket/epoll errors.
  - An error on one client's connection closes only that client, and the reason is logged.
  - When the server runs out of file descriptors, it accepts and immediately closes new
    connections, so it never spins.
- **Persistence.** Stored messages and known users are kept in memory only, and are lost
  when the server stops.

### Server log

One line per event, timestamped in local time with the UTC offset (Common Log Format):

```
[12/Oct/2026:18:23:45 +0000] client 1 RECV SENDMSG,alice1,bob2,hi
[12/Oct/2026:18:23:45 +0000] STATE stored message from alice1 for bob2 (1 waiting for them); 2 connected, 2 logged in, 1 message(s) stored for 1 user(s)
[12/Oct/2026:18:23:45 +0000] client 1 SEND REP_SENDMSG: Message successfully sent to bob2
```

The log records:
- every command received (`RECV`) and every reply sent (`SEND`)
- connects and disconnects, with the reason
- rejected commands and discarded frames, with the reason
- `STATE` lines showing how many clients are connected and messages stored
- queued output (backpressure)
- startup and shutdown

Control characters in client data are written as `\xNN`.

## Client behaviour

- Prints every command sent and every reply received with a timestamp, plus
  informational lines:
  ```
  2026-10-12 18:23:45 SENT: LOGIN,alice
  2026-10-12 18:23:45 RECV: REP_LOGIN:alice1
  2026-10-12 18:23:45 INFO: your user id is alice1
  ```
- Matches each reply to the oldest command still waiting for one, and checks that the
  reply has the format expected for that command. A reply that does not match is
  printed with a warning.
- Uses `poll` to wait on stdin and the server at the same time, with no busy-waiting.
  Incoming bytes are reassembled into complete frames.
- Ignores empty lines and strips a trailing `\r`. A command longer than 5000 bytes is
  not sent, and an error is printed instead.
- When stdin ends (Ctrl-D, or the end of piped input), the client waits up to 5 seconds
  for replies it is still owed, then exits.
- Exits when the server closes the connection.
- Exit status is 0 on a normal end. It is 1 on errors (cannot connect, send/recv failure,
  replies lost or timed out).
- Stdout is line-buffered, so output redirected to a file is written immediately.

## Known limitations

- A stored message whose text is exactly ` No message` (with the leading space) cannot be
  told apart from the `REP_GETMSG: No message` reply.
- A `STATUSREQ` or `LISTUSERS` reply longer than 5000 bytes cannot be framed. The server
  logs an error and sends no reply.
- There is no limit on the number of stored messages per user.
