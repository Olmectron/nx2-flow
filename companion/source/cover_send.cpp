// SPDX-FileCopyrightText: Azahar Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The sending half of toronja's cover share (toronja: src/citra_switch/cover_share.cpp), as
// that file has it: the port, the three words, the layout of a file on the stream, the
// checksum, the direct wireless network's scene id and passphrase are all its own, so that
// whatever receives covers from toronja receives them from this. What is not here is the
// receiving half and the thread: this runs on the program's one thread until it is done.

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <switch.h>

#include "cover_send.h"
#include "hooks.h"

namespace Companion::CoverSend {
namespace {

constexpr u16 PORT = 28301;
constexpr char HELLO[] = "TCOV1";   // Opens the TCP stream.
constexpr char SEEK[] = "TCOV?";    // Broadcast by the sender.
constexpr char HERE[] = "TCOV!";    // The receiver's answer.
constexpr u32 MAX_FILE = 6u << 20;  // No cover is anywhere near this.
constexpr std::size_t MAX_NAME = 40;
constexpr u8 KIND_FRONT = 0, KIND_WRAP = 1, KIND_END = 0xFF;

// Both ends must use the same values.
constexpr u16 LDN_SCENE_ID = 0x4356; // "CV"
constexpr u8 LDN_PASSPHRASE[0x10] = {0x54, 0x6f, 0x72, 0x6f, 0x6e, 0x6a, 0x61, 0x43,
                                     0x6f, 0x76, 0x65, 0x72, 0x73, 0x30, 0x30, 0x31};

std::string Hex(Result rc) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%x", rc);
    return buffer;
}

// ------------------------------------------------------------------------------- sockets

void CloseSocket(int& fd) {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

void SetTimeouts(int fd, int seconds) {
    timeval tv{};
    tv.tv_sec = seconds;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

/// Everything or failure. Gives up when cancelled or when the other end goes quiet.
bool SendAll(int fd, const void* data, std::size_t size) {
    const char* p = static_cast<const char*>(data);
    while (size > 0) {
        if (Cancelled()) {
            return false;
        }
        // (in pieces, so that a stop is seen and the screen kept up between them)
        const ssize_t n = send(fd, p, std::min<std::size_t>(size, 0x8000), 0);
        if (n <= 0) {
            return false;
        }
        p += n;
        size -= static_cast<std::size_t>(n);
    }
    return true;
}

bool RecvAll(int fd, void* data, std::size_t size) {
    char* p = static_cast<char*>(data);
    while (size > 0) {
        if (Cancelled()) {
            return false;
        }
        const ssize_t n = recv(fd, p, size, 0);
        if (n <= 0) {
            return false;
        }
        p += n;
        size -= static_cast<std::size_t>(n);
    }
    return true;
}

bool SendU32(int fd, u32 value) {
    const u32 wire = htonl(value);
    return SendAll(fd, &wire, sizeof(wire));
}

/// FNV-1a: enough to notice a file that arrived damaged.
u32 Checksum(const std::vector<u8>& data) {
    u32 hash = 2166136261u;
    for (const u8 byte : data) {
        hash = (hash ^ byte) * 16777619u;
    }
    return hash;
}

int OpenUdp() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }
    const int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    return fd;
}

/// Connects with a time limit (a plain connect can sit for minutes on a dead address).
int ConnectTo(in_addr address, int seconds) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    const int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr = address;
    int rc = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc != 0 && errno != EINPROGRESS) {
        CloseSocket(fd);
        return -1;
    }
    for (int waited = 0; rc != 0 && waited < seconds * 10; ++waited) {
        if (Cancelled()) {
            CloseSocket(fd);
            return -1;
        }
        pollfd p{fd, POLLOUT, 0};
        if (poll(&p, 1, 100) > 0) {
            int error = 0;
            socklen_t length = sizeof(error);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length);
            if (error != 0 || (p.revents & (POLLERR | POLLHUP)) != 0) {
                CloseSocket(fd);
                return -1;
            }
            rc = 0;
        }
    }
    if (rc != 0) {
        CloseSocket(fd);
        return -1;
    }
    fcntl(fd, F_SETFL, flags);
    SetTimeouts(fd, 15);
    return fd;
}

// --------------------------------------------------------------------------------- files

bool ReadFile(const std::string& path, std::vector<u8>& data) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    const std::size_t got = std::fread(data.data(), 1, data.size(), file);
    std::fclose(file);
    return got == data.size();
}

// ----------------------------------------------------------------------------------- LDN

bool s_ldn_open = false;

void CloseLdn() {
    if (s_ldn_open) {
        ldnExit();
        s_ldn_open = false;
    }
}

LdnSecurityConfig Security() {
    LdnSecurityConfig security{};
    security.security_mode = LdnSecurityMode_Product;
    security.passphrase_size = sizeof(LDN_PASSPHRASE);
    std::memcpy(security.passphrase, LDN_PASSPHRASE, sizeof(LDN_PASSPHRASE));
    return security;
}

/// Finds the receiver's network and joins it. `host` is the receiver.
bool JoinDirectWireless(in_addr& host, std::string& why, int seconds) {
    static LdnNetworkInfo networks[0x18];
    const u64 deadline = armGetSystemTick() + armNsToTicks(static_cast<u64>(seconds) * 1'000'000'000ull);
    Result last = 0;
    bool refused_by_title = false;
    // The system flavour of the service first: the user one only lets a title join networks
    // listed in its own NACP, and the other console may be running as a different title.
    for (int attempt = 0; armGetSystemTick() < deadline && !Cancelled(); ++attempt) {
        const LdnServiceType service = (attempt / 2) % 2 == 0 ? LdnServiceType_System
                                                             : LdnServiceType_User;
        Result rc = ldnInitialize(service);
        if (R_FAILED(rc)) {
            last = rc;
            svcSleepThread(200'000'000);
            continue;
        }
        s_ldn_open = true;
        ldnSetProtocol(attempt % 2 == 0 ? LdnProtocol_Unknown3 : LdnProtocol_NX);
        rc = ldnOpenStation();
        if (R_FAILED(rc)) {
            last = rc;
            CloseLdn();
            svcSleepThread(200'000'000);
            continue;
        }
        for (int scan = 0; scan < 5 && armGetSystemTick() < deadline && !Cancelled(); ++scan) {
            LdnScanFilter filter{};
            filter.network_id.intent_id.local_communication_id = -1;
            filter.network_id.intent_id.scene_id = LDN_SCENE_ID;
            filter.flags = LdnScanFilterFlag_SceneId;
            s32 total = 0;
            rc = ldnScan(0, &filter, networks, 0x18, &total);
            if (R_FAILED(rc) || total <= 0) {
                if (R_FAILED(rc)) {
                    last = rc;
                }
                svcSleepThread(300'000'000);
                continue;
            }
            LdnSecurityConfig security = Security();
            LdnUserConfig user{};
            std::strncpy(user.user_name, "ToronjaCovers", sizeof(user.user_name) - 1);
            rc = ldnConnect(&security, &user, 0, 0, &networks[0]);
            Log("ldnConnect " + Hex(rc));
            if (R_FAILED(rc)) {
                last = rc;
                refused_by_title = refused_by_title || rc == 0xc2cb;
                break; // Another flavour of the service, another protocol.
            }
            LdnNetworkInfo info{};
            rc = ldnGetNetworkInfo(&info);
            if (R_FAILED(rc) || info.node_count == 0) {
                last = rc;
                break;
            }
            // The first node is the access point: the receiver.
            host.s_addr = htonl(info.nodes[0].ip_addr.addr);
            return true;
        }
        CloseLdn();
    }
    if (Cancelled()) {
        why = "stopped";
    } else if (refused_by_title) {
        why = "the system refused the link (0xc2cb): this happens when the program runs over a "
              "game. Open the Homebrew Menu from the Album on this console, or put both "
              "consoles on the same network";
    } else if (last != 0) {
        why = "no console waiting for covers was found nearby (last answer " + Hex(last) + ")";
    } else {
        why = "no console waiting for covers was found nearby";
    }
    return false;
}

// -------------------------------------------------------------------------------- sender

bool FindOnNetwork(in_addr& host, int seconds) {
    int udp = OpenUdp();
    if (udp < 0) {
        return false;
    }
    bool found = false;
    for (int round = 0; round < seconds * 2 && !found && !Cancelled(); ++round) {
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_port = htons(PORT);
        to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        sendto(udp, SEEK, sizeof(SEEK), 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
        // And the broadcast address of a /24 around our own address, for routers that drop
        // the limited one.
        const u32 own = gethostid();
        if (own != 0 && own != htonl(INADDR_LOOPBACK)) {
            to.sin_addr.s_addr = (own & htonl(0xFFFFFF00u)) | htonl(0x000000FFu);
            sendto(udp, SEEK, sizeof(SEEK), 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
        }
        pollfd p{udp, POLLIN, 0};
        if (poll(&p, 1, 500) > 0 && (p.revents & POLLIN) != 0) {
            char answer[16]{};
            sockaddr_in from{};
            socklen_t length = sizeof(from);
            const ssize_t n = recvfrom(udp, answer, sizeof(answer), 0,
                                       reinterpret_cast<sockaddr*>(&from), &length);
            if (n >= static_cast<ssize_t>(sizeof(HERE)) &&
                std::memcmp(answer, HERE, sizeof(HERE)) == 0) {
                host = from.sin_addr;
                found = true;
            }
        }
    }
    CloseSocket(udp);
    return found;
}

} // namespace

bool GoodName(const std::string& name) {
    if (name.empty() || name.size() > MAX_NAME || name.front() == '.') {
        return false;
    }
    const std::size_t dot = name.rfind('.');
    if (dot == std::string::npos) {
        return false;
    }
    std::string ext = name.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (ext != ".jpg" && ext != ".jpeg" && ext != ".png") {
        return false;
    }
    // Letters, digits and the one dot: nothing that could name another folder.
    return std::all_of(name.begin(), name.begin() + static_cast<std::ptrdiff_t>(dot),
                       [](unsigned char ch) { return std::isalnum(ch) != 0; });
}

Outcome Send(const std::vector<Item>& items) {
    Outcome out;
    struct Ready {
        const Item* item;
        u32 size;
    };
    std::vector<Ready> files;
    for (const Item& item : items) {
        struct stat st {};
        if (!GoodName(item.name) || stat(item.path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) ||
            st.st_size <= 0 || static_cast<u64>(st.st_size) > MAX_FILE) {
            continue;
        }
        files.push_back({&item, static_cast<u32>(st.st_size)});
    }
    if (files.empty()) {
        out.words = "There are no covers on this console to send.";
        return out;
    }

    in_addr host{};
    Status("Searching", "Looking for the other console on the network...");
    bool found = FindOnNetwork(host, 4);
    if (!found && !Cancelled()) {
        Status("Searching", "Looking for the other console nearby (direct wireless)...");
        std::string why;
        found = JoinDirectWireless(host, why, 60);
        if (!found && !Cancelled()) {
            out.words = "Covers were not sent: " + why + ".";
        }
    }
    int fd = -1;
    if (found && !Cancelled()) {
        // The receiver may still be opening its sockets on the new link.
        for (int attempt = 0; attempt < 5 && fd < 0 && !Cancelled(); ++attempt) {
            fd = ConnectTo(host, 6);
            if (fd < 0) {
                svcSleepThread(500'000'000);
            }
        }
        if (fd < 0 && !Cancelled()) {
            out.words = "Covers were not sent: the other console was found but did not answer.";
        }
    }

    if (fd >= 0) {
        out.reached = true;
        Peer(std::string(inet_ntoa(host)) + (s_ldn_open ? " (direct wireless)" : " (same network)"));
        const u32 total = static_cast<u32>(files.size());
        out.total = total;
        bool ok = SendAll(fd, HELLO, sizeof(HELLO)) && SendU32(fd, total);
        std::vector<u8> data;
        for (u32 i = 0; ok && i < total; ++i) {
            const Ready& file = files[i];
            data.resize(file.size);
            if (!ReadFile(file.item->path, data)) {
                ++out.refused; // Unreadable here: skip it, the rest can still go.
                continue;
            }
            const std::string& name = file.item->name;
            const u8 head[2] = {file.item->wrap ? KIND_WRAP : KIND_FRONT, static_cast<u8>(name.size())};
            u8 answer = 0;
            ok = SendAll(fd, head, sizeof(head)) && SendAll(fd, name.data(), name.size()) &&
                 SendU32(fd, file.size) && SendU32(fd, Checksum(data)) &&
                 SendAll(fd, data.data(), data.size()) && RecvAll(fd, &answer, 1);
            if (ok) {
                answer == 'K' ? ++out.stored : ++out.refused;
                Status(std::to_string(i + 1) + " / " + std::to_string(total), "Sending covers");
            }
        }
        const u8 end[2] = {KIND_END, 0};
        ok = ok && SendAll(fd, end, sizeof(end));
        CloseSocket(fd);
        if (Cancelled()) {
            out.words = "Sending covers was stopped; " + std::to_string(out.stored) + " had arrived.";
        } else if (!ok) {
            out.words = "The connection was lost while sending; " + std::to_string(out.stored) +
                        " of " + std::to_string(total) + " covers had arrived.";
        } else {
            out.finished = true;
            out.words = std::to_string(out.stored) + (out.stored == 1 ? " cover" : " covers") + " sent" +
                        (out.refused > 0 ? "; " + std::to_string(out.refused) + " could not be stored."
                                         : ".");
        }
    } else if (Cancelled()) {
        out.words = "Sending covers was stopped.";
    }
    Peer({});
    CloseLdn();
    return out;
}

} // namespace Companion::CoverSend
