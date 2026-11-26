// SPDX-FileCopyrightText: Azahar Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Adapted from toronja's cover download (toronja: src/citra_switch/covers.cpp): the order of
// the kinds and of the regions, the secure address first and the plain one when the secure
// connection cannot be made, the ".part" file that becomes the picture only when it is whole.
// toronja asks through libcurl; this asks through a socket and the console's own TLS, so
// that the program needs nothing but libnx.

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <switch.h>

#include "cover_fetch.h"
#include "hooks.h"

namespace Companion::CoverFetch {
namespace {

constexpr char HOST[] = "art.gametdb.com";
constexpr char SECTION[] = "switch"; // The part of the site this family of consoles is filed under.
constexpr std::size_t MAX_BODY = 6u << 20; // What the receiving console takes at most.
constexpr std::size_t MIN_BODY = 1024;     // Less is a page that says there is no picture.

enum class Got { Ok, NotThere, Failed };

bool s_tls_ready = false;
bool s_plain_only = false; // The secure connection cannot be made here: stop trying it.

// ----------------------------------------------------------------------------- connection

/// A connection that is plain or, for https, the system's own TLS over the same socket.
struct Line {
    int sock{-1};
    int given{-1}; // What the TLS service handed back for the socket, to close before it.
    bool tls{};
    SslContext context{};
    SslConnection connection{};
    bool context_open{}, connection_open{};

    bool Send(const char* data, std::size_t size) {
        while (size != 0) {
            u32 done = 0;
            if (tls) {
                if (R_FAILED(sslConnectionWrite(&connection, data, static_cast<u32>(size), &done)) ||
                    done == 0) {
                    return false;
                }
            } else {
                const ssize_t n = send(sock, data, size, 0);
                if (n <= 0) {
                    return false;
                }
                done = static_cast<u32>(n);
            }
            data += done;
            size -= done;
        }
        return true;
    }

    /// Bytes read; 0 at the end; -1 on an error.
    int Receive(char* to, std::size_t size) {
        if (tls) {
            u32 done = 0;
            return R_FAILED(sslConnectionRead(&connection, to, static_cast<u32>(size), &done))
                       ? -1
                       : static_cast<int>(done);
        }
        return static_cast<int>(recv(sock, to, size, 0));
    }

    void Close() {
        if (given >= 0 && given != sock) {
            close(given);
        }
        given = -1;
        if (connection_open) {
            sslConnectionClose(&connection);
        }
        if (context_open) {
            sslContextClose(&context);
        }
        if (sock >= 0) {
            close(sock);
        }
        sock = -1;
        tls = connection_open = context_open = false;
    }
};

/// Connects with a time limit, and so that a stop is seen while it waits.
int Connect(const char* host, const char* port, std::string& error) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(host, port, &hints, &found) != 0 || found == nullptr) {
        error = "the site's address was not found";
        return -1;
    }
    int fd = socket(found->ai_family, found->ai_socktype, found->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(found);
        error = "no socket";
        return -1;
    }
    const int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int rc = connect(fd, found->ai_addr, found->ai_addrlen);
    freeaddrinfo(found);
    bool ok = rc == 0 || errno == EINPROGRESS;
    for (int waited = 0; ok && rc != 0 && waited < 100; ++waited) {
        if (Cancelled()) {
            ok = false;
            break;
        }
        pollfd p{fd, POLLOUT, 0};
        if (poll(&p, 1, 100) > 0) {
            int failure = 0;
            socklen_t length = sizeof(failure);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &failure, &length);
            ok = failure == 0 && (p.revents & (POLLERR | POLLHUP)) == 0;
            rc = 0;
        }
    }
    if (!ok || rc != 0) {
        close(fd);
        error = "the site did not answer";
        return -1;
    }
    fcntl(fd, F_SETFL, flags);
    timeval tv{};
    tv.tv_sec = 15;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    return fd;
}

bool Open(Line& line, const std::string& host, bool tls, std::string& error) {
    line.sock = Connect(host.c_str(), tls ? "443" : "80", error);
    if (line.sock < 0) {
        return false;
    }
    if (!tls) {
        return true;
    }
    if (!s_tls_ready) {
        s_tls_ready = R_SUCCEEDED(sslInitialize(3));
    }
    error = "no secure connection could be made";
    if (!s_tls_ready || R_FAILED(sslCreateContext(&line.context, SslVersion_Auto))) {
        line.Close();
        return false;
    }
    line.context_open = true;
    if (R_FAILED(sslContextCreateConnection(&line.context, &line.connection))) {
        line.Close();
        return false;
    }
    line.connection_open = true;
    line.given = socketSslConnectionSetSocketDescriptor(&line.connection, line.sock);
    if (line.given < 0 && errno != ENOENT) {
        line.Close();
        return false;
    }
    if (R_FAILED(sslConnectionSetHostName(&line.connection, host.c_str(), static_cast<u32>(host.size()))) ||
        R_FAILED(sslConnectionDoHandshake(&line.connection, nullptr, nullptr, nullptr, 0))) {
        error = "the secure connection was refused";
        line.Close();
        return false;
    }
    error.clear();
    line.tls = true;
    return true;
}

// ------------------------------------------------------------------------------- one file

/// The value of a header of the answer's head, or an empty string.
std::string Header(const std::string& head, const char* name) {
    const std::size_t length = std::strlen(name);
    std::size_t at = 0;
    while ((at = head.find("\r\n", at)) != std::string::npos) {
        at += 2;
        if (head.size() - at > length && head[at + length] == ':' &&
            strncasecmp(head.c_str() + at, name, length) == 0) {
            std::size_t from = at + length + 1;
            while (from < head.size() && head[from] == ' ') {
                ++from;
            }
            return head.substr(from, head.find("\r\n", from) - from);
        }
    }
    return {};
}

/// One GET into `path` (through `path`.part). `moved` is where the site sends us instead,
/// when it does.
Got Get(const std::string& url, const std::string& path, std::string& moved, std::string& error) {
    const bool tls = url.rfind("https://", 0) == 0;
    const std::size_t start = url.find("://");
    if (start == std::string::npos) {
        error = "not an address this reads";
        return Got::Failed;
    }
    const std::size_t slash = url.find('/', start + 3);
    const std::string host =
        url.substr(start + 3, slash == std::string::npos ? slash : slash - start - 3);
    const std::string resource = slash == std::string::npos ? "/" : url.substr(slash);
    if (host.empty() || host.size() > 0xff) {
        error = "not an address this reads";
        return Got::Failed;
    }
    Line line;
    if (!Open(line, host, tls, error)) {
        return Got::Failed;
    }
    const std::string request = "GET " + resource + " HTTP/1.0\r\nHost: " + host +
                                "\r\nUser-Agent: covers/1.0\r\nAccept: */*\r\nConnection: close\r\n\r\n";
    if (!line.Send(request.data(), request.size())) {
        error = "the request could not be sent";
        line.Close();
        return Got::Failed;
    }

    static char block[0x4000];
    const std::string part = path + ".part";
    std::string head;
    std::FILE* out = nullptr;
    std::size_t body = 0, expected = 0;
    bool sized = false, whole = false;
    u8 first[2]{};
    std::size_t seen = 0;
    int status = 0;
    Got result = Got::Failed;
    for (;;) {
        if (Cancelled()) {
            error = "stopped";
            break;
        }
        if (sized && out != nullptr && body >= expected) {
            whole = true; // (the site may keep the connection open: the length says it is all here)
            break;
        }
        const int n = line.Receive(block, sizeof(block));
        if (n < 0) {
            error = "the answer was cut short";
            break;
        }
        if (n == 0) {
            whole = out != nullptr && (!sized || body >= expected);
            if (out == nullptr && status == 0) {
                error = "the site closed the connection";
            } else if (out != nullptr && !whole) {
                error = "the answer was cut short";
            }
            break;
        }
        const char* data = block;
        std::size_t size = static_cast<std::size_t>(n);
        if (out == nullptr) {
            head.append(data, size);
            const std::size_t end = head.find("\r\n\r\n");
            if (end == std::string::npos) {
                if (head.size() > 0x4000) {
                    error = "not an answer this reads";
                    break;
                }
                continue;
            }
            std::sscanf(head.c_str(), "HTTP/%*s %d", &status);
            if (status >= 300 && status < 400) {
                moved = Header(head, "Location");
            }
            if (status != 200) {
                if (status >= 300 && status < 500) {
                    result = Got::NotThere;
                } else {
                    error = "the site answered " + std::to_string(status);
                }
                break;
            }
            const std::string length = Header(head, "Content-Length");
            if (!length.empty()) {
                sized = true;
                expected = static_cast<std::size_t>(std::strtoul(length.c_str(), nullptr, 10));
                if (expected > MAX_BODY) {
                    error = "the picture is too large";
                    break;
                }
                if (expected < MIN_BODY) {
                    result = Got::NotThere;
                    break;
                }
            }
            out = std::fopen(part.c_str(), "wb");
            if (out == nullptr) {
                error = "the card could not be written to";
                break;
            }
            head.erase(0, end + 4);
            data = head.data();
            size = head.size();
        }
        for (std::size_t k = 0; k < size && seen < sizeof(first); ++k) {
            first[seen++] = static_cast<u8>(data[k]);
        }
        if (body + size > MAX_BODY) {
            error = "the picture is too large";
            break;
        }
        if (std::fwrite(data, 1, size, out) != size) {
            error = "the card is full or could not be written to";
            break;
        }
        body += size;
    }
    line.Close();
    if (out != nullptr) {
        const bool written = std::fclose(out) == 0;
        // A JPEG or a PNG: anything else is a page, whatever its length.
        const bool picture = seen == 2 && ((first[0] == 0xFF && first[1] == 0xD8) ||
                                           (first[0] == 0x89 && first[1] == 0x50));
        if (whole && !written) {
            error = "the card is full or could not be written to";
        } else if (whole && (body < MIN_BODY || !picture)) {
            result = Got::NotThere;
        } else if (whole) {
            std::remove(path.c_str());
            if (std::rename(part.c_str(), path.c_str()) == 0) {
                return Got::Ok;
            }
            error = "the card could not be written to";
        }
        std::remove(part.c_str());
    }
    return result;
}

/// A picture by its address on the site, following where the site sends us.
Got Download(const std::string& resource, const std::string& path, std::string& error) {
    std::string url = std::string(s_plain_only ? "http://" : "https://") + HOST + resource;
    for (int hop = 0; hop < 4; ++hop) {
        std::string moved;
        Got got = Get(url, path, moved, error);
        if (got == Got::Failed && !Cancelled() && url.rfind("https://", 0) == 0) {
            // A console that cannot make the secure connection (an old system's list of
            // authorities, a wrong clock) can still have the picture plainly: it is a
            // public picture.
            std::string plain_error;
            const Got plain = Get("http://" + url.substr(8), path, moved, plain_error);
            if (plain != Got::Failed) {
                if (!s_plain_only) {
                    Log("The secure connection could not be made (" + error +
                        "): asking plainly from here on.");
                }
                s_plain_only = true;
                error.clear();
                got = plain;
            }
        }
        if (got != Got::NotThere || moved.empty()) {
            return got;
        }
        if (moved.rfind("http://", 0) == 0 || moved.rfind("https://", 0) == 0) {
            url = moved;
        } else if (moved.front() == '/') {
            url = url.substr(0, url.find('/', url.find("://") + 3)) + moved;
        } else {
            return Got::NotThere;
        }
    }
    return Got::NotThere;
}

/// The regions to ask in, the likeliest first, by the last letter of the code.
std::vector<const char*> RegionsFor(const std::string& code) {
    const char last = code.empty() ? ' ' : static_cast<char>(std::toupper(static_cast<unsigned char>(code.back())));
    if (last == 'J') {
        return {"JA", "US", "EN"};
    }
    if (last == 'K') {
        return {"KO", "US", "EN"};
    }
    if (std::strchr("PXYZDFSI", last) != nullptr) {
        return {"EN", "US", "JA"};
    }
    return {"US", "EN", "JA"};
}

bool Exists(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    std::fclose(file);
    return true;
}

} // namespace

std::string FrontPath(const std::string& code) {
    return CoverFolder() + "/" + code + ".jpg";
}

std::string WrapPath(const std::string& code) {
    return CoverFolder() + "/full/" + code + ".jpg";
}

Outcome Fetch(const std::vector<std::string>& codes) {
    Outcome out;
    unsigned wraps = 0, in_a_row = 0;
    std::string none, last_error;
    const std::size_t total = codes.size();
    for (std::size_t i = 0; i < total && !out.stopped; ++i) {
        const std::string& code = codes[i];
        Status(std::to_string(i + 1) + " / " + std::to_string(total), "Downloading " + code);
        // The whole wrap first, then the front: the order toronja asks in.
        struct Kind {
            const char* folder;
            std::string path;
            bool wrap;
        };
        const Kind kinds[] = {{"coverfull", WrapPath(code), true}, {"cover", FrontPath(code), false}};
        bool got_any = false, failed = false;
        std::string what;
        for (const Kind& kind : kinds) {
            if (Exists(kind.path)) {
                continue;
            }
            for (const char* region : RegionsFor(code)) {
                if (Cancelled()) {
                    out.stopped = true;
                    break;
                }
                std::string error;
                const std::string resource =
                    std::string("/") + SECTION + "/" + kind.folder + "/" + region + "/" + code + ".jpg";
                const Got got = Download(resource, kind.path, error);
                if (got == Got::Failed) {
                    failed = true;
                    last_error = error;
                    // Nothing answers: there is no use in asking for every other picture too.
                    if (++in_a_row >= 3) {
                        out.stopped = true;
                    }
                    break;
                }
                in_a_row = 0;
                if (got == Got::Ok) {
                    got_any = true;
                    wraps += kind.wrap ? 1 : 0;
                    what += std::string(what.empty() ? "" : " and ") + (kind.wrap ? "whole wrap" : "front") +
                            " (" + region + ")";
                    break;
                }
            }
            if (out.stopped) {
                break;
            }
        }
        if (got_any) {
            ++out.fetched;
            Log(code + ": " + what);
        } else if (Cancelled()) {
            break;
        } else if (failed) {
            ++out.failed;
            Log(code + ": not downloaded: " + last_error);
        } else if (!Exists(kinds[0].path) && !Exists(kinds[1].path)) {
            ++out.not_there;
            none += (none.empty() ? "" : ", ") + code;
            Log(code + ": GameTDB has no cover under this code");
        }
    }
    if (Cancelled()) {
        out.stopped = true;
        out.words = "Downloading was stopped; " + std::to_string(out.fetched) + " downloaded.";
    } else if (out.stopped) {
        out.words = "Downloading was given up: " + last_error + ". " + std::to_string(out.fetched) +
                    " downloaded.";
    } else {
        out.words = std::to_string(out.fetched) + (out.fetched == 1 ? " cover" : " covers") + " downloaded" +
                    (wraps > 0 ? " (" + std::to_string(wraps) + " with the whole wrap)" : "") +
                    (out.not_there > 0 ? "; none on GameTDB for: " + none : "") +
                    (out.failed > 0 ? "; " + std::to_string(out.failed) + " failed" : "") + ".";
    }
    return out;
}

void Close() {
    if (s_tls_ready) {
        sslExit();
        s_tls_ready = false;
    }
}

} // namespace Companion::CoverFetch
