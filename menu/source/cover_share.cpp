// SPDX-FileCopyrightText: Azahar Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The receiving half of toronja's src/citra_switch/cover_share.cpp. What travels is as it is
// there (the port, the three words, the stream of files, the wireless network's settings); the
// sending half is the companion's. Added: the answer to a sender that asks what is missing.
//
// Only the direct wireless link is the console's own: the rest is the C library's sockets, so
// the exchange was tried on a PC.

#include "cover_share.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __SWITCH__
#include <switch.h>
#else
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
#endif

namespace CoverShare {
namespace {

constexpr u16 PORT = 28301;
constexpr char HELLO[] = "TCOV1";   // Opens the TCP stream.
constexpr char SEEK[] = "TCOV?";    // Broadcast by the sender.
constexpr char HERE[] = "TCOV!";    // The receiver's answer.
constexpr char ASK[] = "TCOVQ";     // Added: the sender asks which covers are missing here.
constexpr u32 MAX_FILE = 6u << 20;  // No cover is anywhere near this.
constexpr std::size_t MAX_NAME = 40;
constexpr u8 KIND_FRONT = 0, KIND_WRAP = 1, KIND_END = 0xFF;
// How long a sender that asked is waited for: it has the pictures to fetch in between.
constexpr int AFTER_QUESTION_SECONDS = 15 * 60;

#ifdef __SWITCH__
// Both ends must use the same values (toronja's).
constexpr u16 LDN_SCENE_ID = 0x4356; // "CV"
constexpr u8 LDN_PASSPHRASE[0x10] = {0x54, 0x6f, 0x72, 0x6f, 0x6e, 0x6a, 0x61, 0x43,
                                     0x6f, 0x76, 0x65, 0x72, 0x73, 0x30, 0x30, 0x31};
#endif

std::thread s_thread;
std::atomic<bool> s_active{false};
std::atomic<bool> s_cancel{false};
std::atomic<bool> s_failed{false};
std::atomic<bool> s_arrived{false};
std::atomic<int> s_serial{0};
std::atomic<unsigned> s_stored{0};
std::mutex s_mutex;
std::string s_brief;
std::string s_detail;
std::vector<std::string> s_lines;
std::string s_folder;
std::vector<Wanted> s_wanted;

/// What the screen shows; `announce` also tells the user (a notice), for steps and endings.
void Status(std::string brief, std::string detail, bool announce, bool failed = false) {
    std::lock_guard lock{s_mutex};
    if (announce && (s_lines.empty() || s_lines.back() != detail)) {
        s_lines.push_back(detail);
        if (s_lines.size() > 8) {
            s_lines.erase(s_lines.begin());
        }
    }
    s_brief = std::move(brief);
    s_detail = std::move(detail);
    if (announce) {
        s_failed = failed;
        ++s_serial;
    }
}

void Fail(std::string detail) {
    Status("Failed", std::move(detail), true, true);
}

bool Cancelled() {
    return s_cancel.load();
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
        const ssize_t n = send(fd, p, size, 0);
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

bool RecvU32(int fd, u32& value) {
    u32 wire = 0;
    if (!RecvAll(fd, &wire, sizeof(wire))) {
        return false;
    }
    value = ntohl(wire);
    return true;
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
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        CloseSocket(fd);
    }
    return fd;
}

int OpenListener() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    const int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(fd, 1) != 0) {
        CloseSocket(fd);
    }
    return fd;
}

// --------------------------------------------------------------------------------- files

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

bool WriteFileSafely(const std::string& path, const std::vector<u8>& data) {
    const std::string partial = path + ".part";
    std::FILE* file = std::fopen(partial.c_str(), "wb");
    if (file == nullptr) {
        return false;
    }
    const std::size_t put = std::fwrite(data.data(), 1, data.size(), file);
    const bool closed = std::fclose(file) == 0;
    if (put != data.size() || !closed) {
        std::remove(partial.c_str());
        return false;
    }
    std::remove(path.c_str());
    if (std::rename(partial.c_str(), path.c_str()) != 0) {
        std::remove(partial.c_str());
        return false;
    }
    return true;
}

// ----------------------------------------------------------------------------------- LDN

#ifdef __SWITCH__

bool s_ldn_open = false;

std::string Hex(Result rc) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%x", rc);
    return buffer;
}

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

/// A network of this console's own for the sender to join.
bool HostDirectWireless(std::string& why) {
    Result rc = ldnInitialize(LdnServiceType_User);
    if (R_FAILED(rc)) {
        why = "ldnInitialize " + Hex(rc);
        return false;
    }
    s_ldn_open = true;
    ldnSetProtocol(LdnProtocol_Unknown3); // Mixed Switch 1 / Switch 2; harmless if refused.
    rc = ldnOpenAccessPoint();
    if (R_FAILED(rc)) {
        why = "ldnOpenAccessPoint " + Hex(rc);
        CloseLdn();
        return false;
    }
    LdnSecurityConfig security = Security();
    LdnUserConfig user{};
    std::strncpy(user.user_name, "ToronjaCovers", sizeof(user.user_name) - 1);
    LdnNetworkConfig network{};
    network.intent_id.local_communication_id = -1;
    network.intent_id.scene_id = LDN_SCENE_ID;
    network.channel = 0;
    network.node_count_max = 2;
    network.local_communication_version = 0;
    rc = ldnCreateNetwork(&security, &user, &network);
    if (R_FAILED(rc)) {
        why = "ldnCreateNetwork " + Hex(rc);
        CloseLdn();
        return false;
    }
    return true;
}

#else

void CloseLdn() {}

bool HostDirectWireless(std::string& why) {
    why = "that is the console's";
    return false;
}

#endif

// ------------------------------------------------------------------------------ receiver

/// The answer to a sender that asks: the games that have no cover here (docs/layout.md).
bool SendWanted(int fd) {
    std::vector<u8> out;
    const auto text = [&out](std::string s) {
        if (s.size() > 255) {
            std::size_t n = 255;
            while (n > 0 && (static_cast<u8>(s[n]) & 0xC0) == 0x80) {
                --n; // (not in the middle of a character)
            }
            s.resize(n);
        }
        out.push_back(static_cast<u8>(s.size()));
        out.insert(out.end(), s.begin(), s.end());
    };
    for (const Wanted& wanted : s_wanted) {
        text(wanted.id);
        text(wanted.code);
        text(wanted.title);
    }
    return SendU32(fd, static_cast<u32>(s_wanted.size())) && SendAll(fd, out.data(), out.size());
}

enum class Got {
    Broken,   // The connection said nothing this understands, or was cut.
    Question, // It asked what is missing, was told, and left: it will be back.
    Stream,   // It sent its files, to the end.
};

/// Takes one sender's connection.
Got Serve(int fd, u32& stored, u32& total) {
    char hello[sizeof(HELLO)]{};
    if (!RecvAll(fd, hello, sizeof(hello))) {
        return Got::Broken;
    }
    if (std::memcmp(hello, ASK, sizeof(ASK)) == 0) {
        if (!SendWanted(fd)) {
            return Got::Broken;
        }
        const std::size_t count = s_wanted.size();
        Status("Asked",
               "The other console asked which covers are missing: " + std::to_string(count) +
                   (count == 1 ? " game." : " games.") + " Waiting for it to come back with them...",
               true);
        // It may go on at once on this connection, or come back on another.
        if (!RecvAll(fd, hello, sizeof(hello))) {
            return Cancelled() ? Got::Broken : Got::Question;
        }
    }
    if (std::memcmp(hello, HELLO, sizeof(HELLO)) != 0 || !RecvU32(fd, total)) {
        return Got::Broken;
    }
    mkdir(s_folder.c_str(), 0777);
    mkdir((s_folder + "/full").c_str(), 0777);
    std::vector<u8> data;
    for (u32 seen = 0;; ++seen) {
        u8 head[2]{};
        if (!RecvAll(fd, head, sizeof(head))) {
            return Got::Broken;
        }
        if (head[0] == KIND_END) {
            return Got::Stream;
        }
        if (head[1] == 0 || head[1] > MAX_NAME || (head[0] != KIND_FRONT && head[0] != KIND_WRAP)) {
            return Got::Broken;
        }
        std::string name(head[1], '\0');
        u32 size = 0, checksum = 0;
        if (!RecvAll(fd, name.data(), name.size()) || !RecvU32(fd, size) ||
            !RecvU32(fd, checksum) || size == 0 || size > MAX_FILE) {
            return Got::Broken;
        }
        data.resize(size);
        if (!RecvAll(fd, data.data(), data.size())) {
            return Got::Broken;
        }
        u8 answer = 'E';
        if (GoodName(name) && Checksum(data) == checksum) {
            const std::string path = s_folder + (head[0] == KIND_WRAP ? "/full/" : "/") + name;
            if (WriteFileSafely(path, data)) {
                answer = 'K';
                ++stored;
                ++s_stored;
            }
        }
        if (!SendAll(fd, &answer, 1)) {
            return Got::Broken;
        }
        Status(std::to_string(seen + 1) + " / " + std::to_string(total), "Receiving covers",
               false);
    }
}

/// Waits for a sender on the sockets given, answering its broadcasts. Returns the accepted
/// connection, or -1 after `seconds` or when cancelled. `heard` says a sender was looking.
int WaitForSender(int listener, int udp, int seconds, bool& heard) {
    for (int waited = 0; waited < seconds * 5 && !Cancelled(); ++waited) {
        pollfd fds[2] = {{listener, POLLIN, 0}, {udp, POLLIN, 0}};
        if (poll(fds, udp >= 0 ? 2 : 1, 200) <= 0) {
            continue;
        }
        if (udp >= 0 && (fds[1].revents & POLLIN) != 0) {
            char question[16]{};
            sockaddr_in from{};
            socklen_t length = sizeof(from);
            const ssize_t n = recvfrom(udp, question, sizeof(question), 0,
                                       reinterpret_cast<sockaddr*>(&from), &length);
            if (n >= static_cast<ssize_t>(sizeof(SEEK)) &&
                std::memcmp(question, SEEK, sizeof(SEEK)) == 0) {
                heard = true;
                sendto(udp, HERE, sizeof(HERE), 0, reinterpret_cast<sockaddr*>(&from), length);
            }
        }
        if ((fds[0].revents & POLLIN) != 0) {
            const int fd = accept(listener, nullptr, nullptr);
            if (fd >= 0) {
                SetTimeouts(fd, 15);
                return fd;
            }
        }
    }
    return -1;
}

struct Outcome {
    bool contact{}; // A sender connected at all.
    bool asked{};   // One asked what is missing.
    bool stream{};  // One began to send.
    bool ok{};      // And finished.
    u32 stored{}, total{};
};

/// Waits on one network until a sender has sent its files, or nobody comes. A sender that
/// asks and leaves is waited for, for longer.
void Attend(int listener, int udp, int seconds, bool& heard, Outcome& out) {
    while (!Cancelled() && !out.stream) {
        int fd = WaitForSender(listener, udp, seconds, heard);
        if (fd < 0) {
            return;
        }
        out.contact = true;
        const Got got = Serve(fd, out.stored, out.total);
        CloseSocket(fd);
        if (got == Got::Question) {
            out.asked = true;
            seconds = AFTER_QUESTION_SECONDS;
            continue;
        }
        out.stream = true;
        out.ok = got == Got::Stream;
    }
}

void ReceiveThread() {
    int listener = OpenListener();
    int udp = OpenUdp();
    bool heard = false;
    Outcome out;
    bool told = false; // Whether how it ended was already said.
    if (listener >= 0) {
        // The network this console is already on comes first: a direct wireless link takes
        // the console off it.
        Status("Waiting", "Waiting for the other console: choose to send the covers there.", true);
        Attend(listener, udp, 8, heard, out);
        if (!out.contact && heard && !Cancelled()) {
            Attend(listener, udp, 20, heard, out);
        }
    }
    if (!out.contact && !Cancelled()) {
        CloseSocket(listener);
        CloseSocket(udp);
        std::string why;
        if (HostDirectWireless(why)) {
            listener = OpenListener();
            udp = OpenUdp();
            if (listener >= 0) {
                Status("Waiting", "Waiting for the other console nearby (direct wireless)...",
                       true);
                Attend(listener, udp, 180, heard, out);
                if (!out.contact && !Cancelled()) {
                    Fail("No console sent covers within three minutes.");
                    told = true;
                }
            } else {
                Fail("Covers cannot be received: the console would not open a connection.");
                told = true;
            }
        } else {
            Fail("Covers cannot be received: no sender on the network, and direct wireless "
                 "is not available here (" + why + ").");
            told = true;
        }
    }

    if (out.stored > 0) {
        s_arrived = true;
    }
    if (out.stream) {
        if (out.ok) {
            Status(std::to_string(out.stored) + " received",
                   std::to_string(out.stored) + (out.stored == 1 ? " cover" : " covers") + " received" +
                       (out.stored < out.total ? "; " + std::to_string(out.total - out.stored) +
                                                     " arrived damaged and were left out."
                                               : "."),
                   true, out.stored == 0 && out.total > 0);
        } else if (Cancelled()) {
            Status("Stopped", "Receiving covers was stopped; " + std::to_string(out.stored) +
                                  " were stored.", true);
        } else {
            Fail("The connection was lost while receiving; " + std::to_string(out.stored) +
                 " covers were stored.");
        }
    } else if (Cancelled()) {
        Status("Stopped", "Receiving covers was stopped.", true);
    } else if (out.asked) {
        Fail("The other console asked which covers are missing and did not come back with them.");
    } else if (!told) {
        Fail("Covers cannot be received: the console would not open a connection.");
    }
    CloseSocket(listener);
    CloseSocket(udp);
    CloseLdn();
    s_active = false;
}

} // namespace

void StartReceive(std::string folder, std::vector<Wanted> wanted) {
    if (s_active.load()) {
        return;
    }
    Stop(); // Joins a thread that has finished.
    {
        std::lock_guard lock{s_mutex};
        s_lines.clear();
        s_brief.clear();
        s_detail.clear();
    }
    s_folder = std::move(folder);
    s_wanted = std::move(wanted);
    s_stored = 0;
    s_cancel = false;
    s_active = true;
    s_thread = std::thread{ReceiveThread};
}

void Stop() {
    s_cancel = true;
    if (s_thread.joinable()) {
        s_thread.join();
    }
    s_active = false;
}

bool IsActive() {
    return s_active.load();
}

std::string Brief() {
    std::lock_guard lock{s_mutex};
    return s_brief;
}

std::string Detail() {
    std::lock_guard lock{s_mutex};
    return s_detail;
}

std::vector<std::string> Lines() {
    std::lock_guard lock{s_mutex};
    return s_lines;
}

int Serial() {
    return s_serial.load();
}

bool Failed() {
    return s_failed.load();
}

unsigned Stored() {
    return s_stored.load();
}

bool TakeArrived() {
    return s_arrived.exchange(false);
}

} // namespace CoverShare
