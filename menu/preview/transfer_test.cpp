// SPDX-License-Identifier: GPL-2.0-or-later

// The menu's file receiver (source/transfer.cpp) on a PC, with a folder for a card: to try it
// against the program that sends, with no console.
//
//   transfer_test <card folder> <seconds to run> [block KB] [queue] [KB a second] [socket buffer KB]
//
// It prints what the Transfer screen would list. The sender is then pointed at 127.0.0.1.
//
//   transfer_test <card folder> split
//
// The receiver tried against a sender of its own, on this PC alone: files written in parts
// (menu.cfg's split=<MB>, what a card that takes no file of 4 GB makes of a large one) and
// whole, each read back and compared byte for byte with what was sent, and a game that arrived
// in parts looked for in the library. The card is one tools/make_parts_card.py made (a
// container to send is taken from it). It says what it checked and ends with 0 when all held.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "library.h"
#include "transfer.h"

namespace {

using u8 = std::uint8_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using Bytes = std::vector<u8>;

int g_failed = 0, g_checked = 0;

void Check(bool held, const std::string& what) {
    ++g_checked;
    if (!held) {
        ++g_failed;
        std::printf("FAILED: %s\n", what.c_str());
    }
}

/// What the Transfer screen would list since the last time this was asked.
void PrintNew(std::string& last) {
    const Transfer::State state = Transfer::Look();
    // (only what is new: the screen keeps the last few lines, so print from the first unseen)
    std::size_t from = 0;
    for (std::size_t n = 0; n < state.lines.size(); ++n) {
        if (state.lines[n] == last) {
            from = n + 1;
        }
    }
    for (std::size_t n = from; n < state.lines.size(); ++n) {
        std::printf("%s\n", state.lines[n].c_str());
        last = state.lines[n];
    }
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------------- a sender

bool SendAll(int fd, const void* data, std::size_t size) {
    const char* p = static_cast<const char*>(data);
    while (size > 0) {
        const ssize_t sent = send(fd, p, size, 0);
        if (sent <= 0) {
            return false;
        }
        p += sent;
        size -= static_cast<std::size_t>(sent);
    }
    return true;
}

bool RecvAll(int fd, void* data, std::size_t size) {
    char* p = static_cast<char*>(data);
    while (size > 0) {
        const ssize_t got = recv(fd, p, size, 0);
        if (got <= 0) {
            return false;
        }
        p += got;
        size -= static_cast<std::size_t>(got);
    }
    return true;
}

/// The receiver's answer: true for "done", or false and what it says is wrong.
bool Answer(int fd, std::string& said) {
    u8 status = 1, length[2] = {0, 0};
    if (!RecvAll(fd, &status, 1)) {
        said = "no answer";
        return false;
    }
    if (status == 0) {
        return true;
    }
    if (RecvAll(fd, length, 2)) {
        said.assign(static_cast<std::size_t>(length[0] | length[1] << 8), '\0');
        RecvAll(fd, said.data(), said.size());
    }
    return false;
}

/// zlib's CRC-32, a bit at a time: the sum the receiver answers a file with.
u32 Crc(const Bytes& data) {
    u32 crc = ~0u;
    for (const u8 byte : data) {
        crc ^= byte;
        for (int k = 0; k < 8; ++k) {
            crc = (crc & 1) ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
        }
    }
    return ~crc;
}

/// One file sent as the program that sends does it (protocol TTX1, PUT), on a connection of
/// its own. True when the receiver took it and its sum of what arrived is ours.
bool Put(const std::string& card_path, const Bytes& data, std::string& said) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(Transfer::kPort);
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (fd < 0 || connect(fd, reinterpret_cast<sockaddr*>(&to), sizeof to) != 0) {
        said = "no connection to the receiver";
        if (fd >= 0) {
            close(fd);
        }
        return false;
    }
    Bytes head = {'T', 'T', 'X', '1', 2, static_cast<u8>(card_path.size()), static_cast<u8>(card_path.size() >> 8)};
    head.insert(head.end(), card_path.begin(), card_path.end());
    const u64 size = data.size();
    for (int k = 0; k < 8; ++k) {
        head.push_back(static_cast<u8>(size >> (8 * k)));
    }
    u8 sum[4] = {};
    bool ok = SendAll(fd, head.data(), head.size()) && Answer(fd, said);
    if (ok) {
        ok = SendAll(fd, data.data(), data.size()) && Answer(fd, said) && RecvAll(fd, sum, 4);
        if (ok && (static_cast<u32>(sum[0]) | static_cast<u32>(sum[1]) << 8 | static_cast<u32>(sum[2]) << 16 | static_cast<u32>(sum[3]) << 24) != Crc(data)) {
            said = "the receiver's sum of the file isn't ours";
            ok = false;
        }
    }
    close(fd);
    return ok;
}

// -------------------------------------------------------------------------------- the checks

Bytes FileBytes(const std::string& card_path) {
    Bytes bytes;
    if (std::FILE* f = std::fopen(Flow::OnCard(card_path).c_str(), "rb")) {
        std::vector<u8> block(1 << 16);
        for (std::size_t got; (got = std::fread(block.data(), 1, block.size(), f)) != 0;) {
            bytes.insert(bytes.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(got));
        }
        std::fclose(f);
    }
    return bytes;
}

/// Bytes that are the same every time and repeat nowhere: `head`, then noise up to `size`.
Bytes Payload(const Bytes& head, std::size_t size, u32 seed) {
    Bytes data = head;
    data.resize(std::max(size, head.size()));
    u32 x = seed * 2654435761u + 1;
    for (std::size_t k = head.size(); k < data.size(); ++k) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        data[k] = static_cast<u8>(x >> 11);
    }
    return data;
}

std::string PartName(const std::string& card_path, int number) {
    char ending[16];
    std::snprintf(ending, sizeof ending, ".%03d", number);
    return card_path + ending;
}

/// Sends a file and looks at what is on the card after: `parts` files of `each` bytes (the last
/// with what is left) that joined are what was sent, or with parts 0 the whole file; and in
/// either case nothing else of that name.
void SendAndCheck(const std::string& card_path, const Bytes& data, int parts, std::size_t each) {
    std::string said;
    Check(Put(card_path, data, said), card_path + " sent: " + said);
    Bytes joined;
    for (int k = 1; k <= parts; ++k) {
        const Bytes one = FileBytes(PartName(card_path, k));
        const std::size_t due = k < parts ? each : data.size() - each * static_cast<std::size_t>(parts - 1);
        Check(one.size() == due, PartName(card_path, k) + " is " + std::to_string(due) + " bytes: " + std::to_string(one.size()));
        joined.insert(joined.end(), one.begin(), one.end());
    }
    if (parts == 0) {
        joined = FileBytes(card_path);
    }
    Check(joined == data, card_path + (parts ? ": its parts joined are what was sent" : ": the file is what was sent"));
    Check(Flow::Exists(card_path) == (parts == 0), card_path + (parts ? " is not there whole as well" : " is there whole"));
    Check(!Flow::Exists(PartName(card_path, parts + 1)), card_path + " has no part " + std::to_string(parts + 1));
    Check(!Flow::Exists(card_path + ".ttpart") && !Flow::Exists(PartName(card_path, 1) + ".ttpart"), card_path + ": nothing left of its arriving");
}

bool StartReceiver(const Transfer::Tuning& tuning) {
    Transfer::Start(tuning);
    for (int k = 0; k < 50 && !Transfer::Look().listening; ++k) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return Transfer::Look().listening;
}

int SplitTest() {
    constexpr std::size_t kMb = 1 << 20;
    const std::string games = Flow::kDefaultGames;
    const Bytes container = FileBytes(games + "/Whole.nup");
    if (container.empty()) {
        std::printf("no %s/Whole.nup on this card: make it with tools/make_parts_card.py\n", games.c_str());
        return 1;
    }
    std::uint64_t title_id = 0;
    for (const Flow::Game& game : Flow::ScanGames(games)) {
        if (game.path == games + "/Whole.nup") {
            title_id = game.title_id;
        }
    }

    // menu.cfg's split, read as the menu reads it. Blocks that end nowhere near a megabyte, so
    // that a block has to be shared between two parts.
    Flow::Settings cfg = Flow::ReadSettings(Flow::kMenuCfg);
    cfg["split"] = "1";
    Flow::WriteSettings(Flow::kMenuCfg, cfg);
    Transfer::Tuning tuning;
    tuning.split_mb = std::atoi(Flow::ReadSettings(Flow::kMenuCfg)["split"].c_str());
    tuning.block_kb = 192;
    std::string last;
    if (!StartReceiver(tuning)) {
        PrintNew(last);
        std::printf("the receiver isn't listening (is port %d taken?)\n", Transfer::kPort);
        Transfer::Stop();
        return 1;
    }

    // A game larger than a part, where the same game was whole before.
    const std::string sent = games + "/Sent.nup";
    if (std::FILE* f = std::fopen(Flow::OnCard(sent).c_str(), "wb")) {
        std::fwrite(container.data(), 1, container.size(), f);
        std::fclose(f);
    }
    SendAndCheck(sent, Payload(container, 2 * kMb + kMb / 2 + 123, 1), 3, kMb);
    // The same again, shorter: the part it has no more use for goes.
    const Bytes shorter = Payload(container, kMb + kMb / 2, 2);
    SendAndCheck(sent, shorter, 2, kMb);
    // A file of exactly the size is whole (parts are for a larger one); of twice the size, two full parts.
    SendAndCheck("/nx2-flow/Exactly.bin", Payload({}, kMb, 3), 0, 0);
    SendAndCheck("/nx2-flow/Twice.bin", Payload({}, 2 * kMb, 4), 2, kMb);
    SendAndCheck("/nx2-flow/small.txt", Payload({}, 300, 5), 0, 0);
    SendAndCheck("/nx2-flow/empty.txt", {}, 0, 0);
    // More parts than the menu and the loader read of one file: whole, then.
    SendAndCheck("/nx2-flow/Many.bin", Payload({}, 64 * kMb + 1, 6), 0, 0);
    SendAndCheck("/nx2-flow/Most.bin", Payload({}, 64 * kMb, 7), 64, kMb);
    PrintNew(last);
    Transfer::Stop();

    // What arrived in parts is one game of the library, the game the container is of.
    bool listed = false;
    for (const Flow::Game& game : Flow::ScanGames(games)) {
        if (game.path == sent + ".001") {
            listed = true;
            Check(game.parts.size() == 2 && game.size == shorter.size(), sent + " is listed in 2 parts, of the size sent");
            Check(game.title_id == title_id && title_id != 0 && game.error.empty(), sent + " is the game its container is of");
        }
        Check(game.path != sent && game.path != sent + ".002", game.path + " is not listed on its own");
    }
    Check(listed, sent + ".001 is listed as a game");

    // Blocks that fill a part exactly; then with no split asked for, the whole file as ever.
    tuning.block_kb = 1024;
    Check(StartReceiver(tuning), "the receiver listens again");
    SendAndCheck("/nx2-flow/Twice.bin", Payload({}, 3 * kMb, 8), 3, kMb);
    Transfer::Stop();
    cfg.erase("split");
    Flow::WriteSettings(Flow::kMenuCfg, cfg);
    tuning.split_mb = std::atoi(Flow::ReadSettings(Flow::kMenuCfg)["split"].c_str());
    Check(tuning.split_mb == 0 && StartReceiver(tuning), "the receiver listens with no split asked for");
    SendAndCheck(games + "/Again.nup", Payload(container, 2 * kMb + kMb / 2, 9), 0, 0);
    PrintNew(last);
    Transfer::Stop();

    std::printf("%d checks, %d failed\n", g_checked, g_failed);
    return g_failed == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("transfer_test <card folder> <seconds> [block KB] [queue] [KB a second] [socket buffer KB]\n"
                    "transfer_test <card folder> split\n");
        return 1;
    }
    Flow::SetCardRoot(argv[1]);
    Flow::MakeFolders(Flow::kHome);
    if (std::strcmp(argv[2], "split") == 0) {
        return SplitTest();
    }
    Transfer::Tuning tuning;
    if (argc > 3) {
        tuning.block_kb = std::atoi(argv[3]);
    }
    if (argc > 4) {
        tuning.queue = std::atoi(argv[4]);
    }
    if (argc > 5) {
        tuning.kbps = std::atoi(argv[5]);
    }
    if (argc > 6) {
        tuning.rcvbuf_kb = std::atoi(argv[6]);
    }
    // (menu.cfg's split, as the menu takes it: files over that many MB arrive in parts)
    tuning.split_mb = std::atoi(Flow::ReadSettings(Flow::kMenuCfg)["split"].c_str());
    Transfer::Start(tuning);
    std::string last;
    const int ticks = std::atoi(argv[2]) * 10;
    for (int k = 0; k < ticks; ++k) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        PrintNew(last);
    }
    Transfer::Stop();
    const Transfer::State state = Transfer::Look();
    std::printf("%u files, %llu bytes\n", state.files, static_cast<unsigned long long>(state.bytes));
    return 0;
}
