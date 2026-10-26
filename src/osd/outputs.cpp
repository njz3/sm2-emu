//  ____  __  __  ____         _____ __  __ _   _
// / ___||  \/  ||___ \       | ____|  \/  | | | |
// \___ \| |\/| |  __) |_____ |  _| | |\/| | | | |
//  ___) | |  | | / __/|_____|| |___| |  | | |_| |
// |____/|_|  |_||_____|      |_____|_|  |_|\___/
//
// A Sega Model 2 arcade emulator.
// Copyright (c) 2025+ Daniel Martin (dmanlfc)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This header must not be removed. sm2-emu is licensed under the GNU General
// Public License, version 3 or later, with additional terms under section 7 of
// that licence. See LICENSE and NOTICE.
//
//
// See outputs.h.

#include "osd/outputs.h"

#include "core/log.h"
#include "core/net.h"
#include "osd/i18n.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <span>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#    include <windows.h>
#endif

namespace sm2::osd {

class Outputs::Backend {
public:
    virtual ~Backend() = default;

    virtual void start(const std::string& game, const std::vector<Item>& items) = 0;
    virtual void stop() = 0;
    virtual void changed(usize index, const Item& item) = 0;
    virtual void paused(bool paused) = 0;
    virtual void poll(const std::string& game, const std::vector<Item>& items,
                      bool paused) = 0;
    [[nodiscard]] virtual NetworkStatus status() const { return {}; }
    [[nodiscard]] virtual std::optional<u8> drive_reply() const { return std::nullopt; }
};

namespace {

// ---------------------------------------------------------------------------
// Lamp names
// ---------------------------------------------------------------------------

constexpr int kRawDrive   = -1;
constexpr int kRawLamps   = -2;
constexpr int kGameOutput = -3;  ///< One of the set's own, from games.xml.
constexpr int kDriveHex   = -4;  ///< m2ffb: the drive board's bytes again, in hex.

constexpr u32 kWorkRamBase = 0x00500000;

/// The byte at a work RAM address, or zero when there is no such byte.
u8 ram_byte(std::span<const u8> ram, u32 address)
{
    const u32 offset = address - kWorkRamBase;
    return offset < ram.size() ? ram[offset] : 0;
}

/// The value of one of a set's own outputs. The i960 is little-endian.
s32 ram_value(std::span<const u8> ram, const rom::OutputSpec& spec)
{
    u32 raw = 0;
    for (u32 i = 0; i < 4; ++i) {
        raw |= static_cast<u32>(ram_byte(ram, spec.address + i)) << (8 * i);
    }
    switch (spec.type) {
    case rom::OutputSpec::Type::U8:
        return static_cast<s32>(raw & 0xff);
    case rom::OutputSpec::Type::U16:
        return static_cast<s32>(raw & 0xffff);
    case rom::OutputSpec::Type::S16:
        return std::max<s32>(static_cast<s16>(raw & 0xffff), 0);
    case rom::OutputSpec::Type::U32:
        return static_cast<s32>(std::min<u32>(raw, 0x7fffffff));
    case rom::OutputSpec::Type::Float: {
        float value = 0.0f;
        std::memcpy(&value, &raw, sizeof value);
        // The comparison also turns a NaN into zero.
        return value > 0.0f ? static_cast<s32>(std::min(value, 2147483520.0f)) : 0;
    }
    }
    return 0;
}

struct NamedBit {
    const char* name;
    int         bit;
};

/// Friendly names for the lamp latch, per set family, from MAME's model2.cpp
/// output notes. Bit 2 is the start lamp everywhere.
std::vector<NamedBit> family_lamps(const std::string& family)
{
    if (family == "daytona") {
        return {{"LampView1", 3}, {"LampView2", 4}, {"LampView3", 5},
                {"LampView4", 6}, {"LampLeader", 7}};
    }
    if (family == "desert") {
        return {{"LampView3", 3}, {"LampView2", 4}, {"LampView1", 5},
                {"MachineGunMotor", 6}, {"CannonMotor", 7}};
    }
    if (family == "srallyc") {
        return {{"LampView1", 5}, {"LampLeader", 7}};
    }
    if (family == "stcc") {
        return {{"LampRevMax", 3}, {"LampView1", 4}, {"LampView2", 5}};
    }
    if (family == "indy500") {
        return {{"LampView1", 4}, {"LampView2", 5}, {"LampLeader", 7}};
    }
    if (family == "overrev" || family == "sgt24h") {
        return {{"LampView1", 4}, {"LampView2", 5}};
    }
    return {};
}

/// What a lamp-latch output means, for the settings' list: a %d format when
/// it sets *arg.
const char* lamp_meaning(const std::string& name, int bit, int* arg)
{
    if (name == "LampStart") {
        return N_("Start button lamp");
    }
    if (name == "LampLeader") {
        return N_("Leader lamp");
    }
    if (name == "LampRevMax") {
        return N_("Rev limit lamp");
    }
    if (name == "MachineGunMotor") {
        return N_("Machine gun motor");
    }
    if (name == "CannonMotor") {
        return N_("Cannon motor");
    }
    if (name.size() == 9 && name.rfind("LampView", 0) == 0) {
        *arg = name[8] - '0';
        return N_("View change button %d lamp");
    }
    *arg = bit;
    return N_("Lamp latch bit %d");
}

/// What one of a set's own outputs means: DemulShooter's names, which the
/// games use, else what its kind says.
const char* game_output_meaning(const rom::OutputSpec& spec, int* arg)
{
    struct Known {
        std::string_view name;
        const char*      meaning;
    };
    static constexpr Known kPlayer[] = {
        {"Life", N_("Player %d's life")},
        {"Damaged", N_("Player %d hit: a pulse each time the life drops")},
        {"Ammo", N_("Player %d's ammunition")},
        {"Clip", N_("Player %d's gun loaded: 1 while it has ammunition")},
        {"CtmRecoil", N_("Player %d's gun recoil: a pulse each shot")},
        {"Shots", N_("Shots player %d has fired")},
        {"GunMotor", N_("Player %d's gun motor")},
        {"LmpStart", N_("Player %d start lamp")},
        {"Wins", N_("Rounds player %d has won in this match")},
    };
    const std::string_view name = spec.name;
    if (name.size() > 3 && name[0] == 'P' && (name[1] == '1' || name[1] == '2') && name[2] == '_') {
        for (const Known& known : kPlayer) {
            if (name.substr(3) == known.name) {
                *arg = name[1] - '0';
                return known.meaning;
            }
        }
    }
    if (name == "Credits") {
        return N_("Credits");
    }
    switch (spec.kind) {
    case rom::OutputSpec::Kind::Lamp:
        *arg = spec.bit;
        return N_("Lamp latch bit %d");
    case rom::OutputSpec::Kind::Value:
        return N_("A number from the game's work RAM");
    case rom::OutputSpec::Kind::Flag:
        return N_("1 while the value it follows is above zero");
    case rom::OutputSpec::Kind::Pulse:
        return N_("A pulse when the value it follows changes");
    }
    return "";
}

std::string hex_byte(u8 value)
{
    char hex[8];
    std::snprintf(hex, sizeof hex, "%02X", static_cast<unsigned>(value));
    return hex;
}

std::string line(std::string_view name, s32 value)
{
    return std::string(name) + " = " + std::to_string(value) + "\r";
}

std::string line(const Outputs::Item& item)
{
    if (!item.hex) {
        return line(item.name, item.value);
    }
    char hex[8];
    std::snprintf(hex, sizeof hex, "%02X", static_cast<unsigned>(item.value & 0xff));
    return item.name + " = " + hex + "\r";
}

/// "m2ffb_rx = XX", BackForceFeeder's reply from the real drive board: its byte.
std::optional<u8> parse_drive_reply(std::string_view text)
{
    const auto trim = [](std::string_view v) {
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) {
            v.remove_prefix(1);
        }
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r')) {
            v.remove_suffix(1);
        }
        return v;
    };
    const usize equals = text.find('=');
    if (equals == std::string_view::npos || trim(text.substr(0, equals)) != "m2ffb_rx") {
        return std::nullopt;
    }
    const std::string_view value = trim(text.substr(equals + 1));
    if (value.empty() || value.size() > 2) {
        return std::nullopt;
    }
    unsigned byte = 0;
    for (const char c : value) {
        const int digit = c >= '0' && c <= '9'   ? c - '0'
                        : c >= 'a' && c <= 'f' ? c - 'a' + 10
                        : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                               : -1;
        if (digit < 0) {
            return std::nullopt;
        }
        byte = byte * 16 + static_cast<unsigned>(digit);
    }
    return static_cast<u8>(byte);
}

// ---------------------------------------------------------------------------
// Network
// ---------------------------------------------------------------------------

class NetworkBackend final : public Outputs::Backend {
public:
    NetworkBackend(u16 port, u16 announce_port) : m_port(port), m_announce_port(announce_port)
    {
        m_started = net::startup();
        if (m_started && m_server.open(port)) {
            SM2_INFO("outputs: network outputs on port %u", port);
        } else if (m_server.address_in_use()) {
            m_error = "port " + std::to_string(port) + " is already in use";
            SM2_WARN("outputs: TCP port %u is already in use, probably by another emulator "
                     "or output tool; close it, or set outputs_network_port to a free port",
                     port);
        } else {
            m_error = m_server.last_error();
            SM2_WARN("outputs: %s", m_server.last_error().c_str());
        }

        // Supermodel's announcement goes to this machine, where the tools run.
        if (m_started && m_announce_port != 0 && !m_announce.open("127.0.0.1", 0)) {
            SM2_WARN("outputs: no UDP announcement: %s", m_announce.last_error().c_str());
        }
    }

    ~NetworkBackend() override
    {
        m_server.close();
        if (m_started) {
            net::shutdown();
        }
    }

    void start(const std::string& game, const std::vector<Outputs::Item>& items) override
    {
        forget_drive_reply();
        m_server.broadcast("mame_start = " + game + "\r");
        for (const Outputs::Item& item : items) {
            if (item.known) {
                m_server.broadcast(line(item));
            }
        }
        announce(game);
    }

    void stop() override
    {
        forget_drive_reply();
        m_server.broadcast("mame_stop = 1\r");
    }

    void changed(usize /*index*/, const Outputs::Item& item) override
    {
        m_server.broadcast(line(item));
    }

    void paused(bool paused) override { m_server.broadcast(line("pause", paused ? 1 : 0)); }

    void poll(const std::string& game, const std::vector<Outputs::Item>& items,
              bool paused) override
    {
        m_server.poll([&](u64 client) {
            if (game.empty()) {
                return;
            }
            m_server.send(client, "mame_start = " + game + "\r");
            for (const Outputs::Item& item : items) {
                if (item.known) {
                    m_server.send(client, line(item));
                }
            }
            if (paused) {
                m_server.send(client, "pause = 1\r");
            }
        });

        // What the clients send, read every frame (a client whose lines pile
        // up is dropped): BackForceFeeder's reply from the real drive board,
        // once it has had an m2ffb line.
        for (const net::TcpServer::Line& in : m_server.take_lines()) {
            if (const std::optional<u8> reply = parse_drive_reply(in.text)) {
                if (!m_reply) {
                    SM2_INFO("outputs: the drive board's answer now comes from a real board "
                             "(m2ffb_rx)");
                }
                m_reply        = reply;
                m_reply_client = in.client;
            }
        }
        if (m_reply && !m_server.has_client(m_reply_client)) {
            SM2_INFO("outputs: the real drive board's client left: the emulated answer again");
            forget_drive_reply();
        }
    }

    [[nodiscard]] std::optional<u8> drive_reply() const override { return m_reply; }

    [[nodiscard]] Outputs::NetworkStatus status() const override
    {
        return {true, m_server.valid(), m_server.client_count(), m_error};
    }

private:
    /// Supermodel's datagram, "mame_start = <set>\rtcp = <port>\r", sent once
    /// when a game starts, provided the server listens.
    void announce(const std::string& game)
    {
        if (game.empty() || !m_announce.valid() || !m_server.valid()) {
            return;
        }
        const std::string text =
            "mame_start = " + game + "\rtcp = " + std::to_string(m_port) + "\r";
        const auto* bytes = reinterpret_cast<const u8*>(text.data());
        static_cast<void>(
            m_announce.send_to(std::span<const u8>(bytes, text.size()), "127.0.0.1", m_announce_port));
    }

    void forget_drive_reply()
    {
        m_reply.reset();
        m_reply_client = 0;
    }

    net::TcpServer m_server;
    net::UdpSocket m_announce;
    u16            m_port          = 0;
    u16            m_announce_port = 0;
    std::string    m_error;
    bool           m_started = false;

    /// The real drive board's last answer, and the client that sent it.
    std::optional<u8> m_reply;
    u64               m_reply_client = 0;
};

// ---------------------------------------------------------------------------
// Windows messages
// ---------------------------------------------------------------------------

#if defined(_WIN32)

/// MAME numbers outputs from here; 0 is the set name and 1 is "pause".
constexpr u32 kFirstId = 12345;
constexpr u32 kPauseId = 1;

class WindowsBackend final : public Outputs::Backend {
public:
    WindowsBackend()
    {
        m_start      = RegisterWindowMessageA("MAMEOutputStart");
        m_stop       = RegisterWindowMessageA("MAMEOutputStop");
        m_update     = RegisterWindowMessageA("MAMEOutputUpdateState");
        m_register   = RegisterWindowMessageA("MAMEOutputRegister");
        m_unregister = RegisterWindowMessageA("MAMEOutputUnregister");
        m_get_id     = RegisterWindowMessageA("MAMEOutputGetIDString");

        WNDCLASSA wc{};
        wc.lpfnWndProc   = &WindowsBackend::window_proc;
        wc.hInstance     = GetModuleHandleA(nullptr);
        wc.lpszClassName = "MAMEOutput";
        RegisterClassA(&wc);  // Fails harmlessly if already registered.

        m_hwnd = CreateWindowExA(0, "MAMEOutput", "MAMEOutput", WS_OVERLAPPEDWINDOW, 0, 0,
                                 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
        if (m_hwnd == nullptr) {
            SM2_WARN("outputs: could not create the MAMEOutput window");
            return;
        }
        SetWindowLongPtrA(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        SM2_INFO("outputs: Windows message outputs enabled");
    }

    ~WindowsBackend() override
    {
        if (m_hwnd != nullptr) {
            DestroyWindow(m_hwnd);
        }
    }

    void start(const std::string& game, const std::vector<Outputs::Item>& items) override
    {
        m_game  = game;
        m_items = &items;
        if (m_hwnd != nullptr) {
            PostMessageA(HWND_BROADCAST, m_start, reinterpret_cast<WPARAM>(m_hwnd), 0);
        }
    }

    void stop() override
    {
        if (m_hwnd != nullptr) {
            PostMessageA(HWND_BROADCAST, m_stop, reinterpret_cast<WPARAM>(m_hwnd), 0);
        }
        m_clients.clear();
        m_game.clear();
        m_items = nullptr;
    }

    void changed(usize index, const Outputs::Item& item) override
    {
        for (const Client& client : m_clients) {
            PostMessageA(client.hwnd, m_update, kFirstId + index,
                         static_cast<LPARAM>(item.value));
        }
    }

    void paused(bool paused) override
    {
        m_paused = paused;
        for (const Client& client : m_clients) {
            PostMessageA(client.hwnd, m_update, kPauseId, paused ? 1 : 0);
        }
    }

    // SDL's event pump dispatches this thread's window messages.
    void poll(const std::string&, const std::vector<Outputs::Item>&, bool) override {}

private:
    struct Client {
        HWND   hwnd;
        LPARAM id;
    };

    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam,
                                        LPARAM lparam)
    {
        auto* self = reinterpret_cast<WindowsBackend*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
        if (self != nullptr) {
            if (message == self->m_register) {
                return self->register_client(reinterpret_cast<HWND>(wparam), lparam);
            }
            if (message == self->m_unregister) {
                return self->unregister_client(lparam);
            }
            if (message == self->m_get_id) {
                return self->send_id_string(reinterpret_cast<HWND>(wparam), lparam);
            }
        }
        return DefWindowProcA(hwnd, message, wparam, lparam);
    }

    LRESULT register_client(HWND hwnd, LPARAM id)
    {
        LRESULT result = 0;
        bool    found  = false;
        for (Client& client : m_clients) {
            if (client.id == id) {
                client.hwnd = hwnd;
                found       = true;
                result      = 1;
            }
        }
        if (!found) {
            m_clients.push_back({hwnd, id});
        }
        if (m_items != nullptr) {
            for (usize i = 0; i < m_items->size(); ++i) {
                if ((*m_items)[i].known) {
                    PostMessageA(hwnd, m_update, kFirstId + i,
                                 static_cast<LPARAM>((*m_items)[i].value));
                }
            }
        }
        PostMessageA(hwnd, m_update, kPauseId, m_paused ? 1 : 0);
        return result;
    }

    LRESULT unregister_client(LPARAM id)
    {
        for (usize i = 0; i < m_clients.size(); ++i) {
            if (m_clients[i].id == id) {
                m_clients.erase(m_clients.begin() + static_cast<std::ptrdiff_t>(i));
                return 0;
            }
        }
        return 1;
    }

    LRESULT send_id_string(HWND hwnd, LPARAM id)
    {
        std::string name;
        if (id == 0) {
            name = m_game;
        } else if (id == kPauseId) {
            name = "pause";
        } else if (m_items != nullptr && id >= static_cast<LPARAM>(kFirstId)
                   && static_cast<usize>(id - kFirstId) < m_items->size()) {
            name = (*m_items)[static_cast<usize>(id - kFirstId)].name;
        }

        // MAME's copydata_id_string: a u32 id followed by the NUL-terminated name.
        std::vector<char> buffer(sizeof(u32) + name.size() + 1, '\0');
        const u32 id32 = static_cast<u32>(id);
        std::memcpy(buffer.data(), &id32, sizeof id32);
        std::memcpy(buffer.data() + sizeof id32, name.data(), name.size());

        COPYDATASTRUCT copydata{};
        copydata.dwData = 1;  // COPYDATA_MESSAGE_ID_STRING
        copydata.cbData = static_cast<DWORD>(buffer.size());
        copydata.lpData = buffer.data();
        SendMessageA(hwnd, WM_COPYDATA, reinterpret_cast<WPARAM>(m_hwnd),
                     reinterpret_cast<LPARAM>(&copydata));
        return 0;
    }

    HWND                              m_hwnd  = nullptr;
    UINT                              m_start = 0, m_stop = 0, m_update = 0;
    UINT                              m_register = 0, m_unregister = 0, m_get_id = 0;
    std::vector<Client>               m_clients;
    std::string                       m_game;
    const std::vector<Outputs::Item>* m_items  = nullptr;
    bool                              m_paused = false;
};

#endif

}  // namespace

// ---------------------------------------------------------------------------
// Outputs
// ---------------------------------------------------------------------------

Outputs::Outputs() = default;

Outputs::~Outputs()
{
    set_game({}, {});
}

void Outputs::configure(bool network, u16 port, u16 announce_port, bool windows)
{
    if (network && (m_network == nullptr || port != m_port || announce_port != m_announce_port)) {
        m_network.reset();
        m_network       = std::make_unique<NetworkBackend>(port, announce_port);
        m_port          = port;
        m_announce_port = announce_port;
        if (!m_game.empty()) {
            m_network->start(m_game, m_items);
        }
    } else if (!network && m_network != nullptr) {
        if (!m_game.empty()) {
            m_network->stop();
            m_network->poll(m_game, m_items, m_paused);
        }
        m_network.reset();
    }

#if defined(_WIN32)
    if (windows && m_windows == nullptr) {
        m_windows = std::make_unique<WindowsBackend>();
        if (!m_game.empty()) {
            m_windows->start(m_game, m_items);
        }
    } else if (!windows && m_windows != nullptr) {
        if (!m_game.empty()) {
            m_windows->stop();
        }
        m_windows.reset();
    }
#else
    static_cast<void>(windows);
#endif
}

void Outputs::set_game(const std::string& name, const std::string& parent,
                       std::span<const rom::OutputSpec> game_outputs, bool drive_board)
{
    if (name == m_game) {
        return;
    }
    for (Backend* backend : {m_network.get(), m_windows.get()}) {
        if (backend != nullptr && !m_game.empty()) {
            backend->stop();
        }
    }
    m_game = name;
    m_items.clear();
    m_bits.clear();
    m_game_outputs.clear();
    if (m_game.empty()) {
        poll();
        return;
    }
    m_family      = parent.empty() ? name : parent;
    m_drive_board = drive_board;
    build_items(m_family);
    if (drive_board) {
        // Sent from the game's first write, under the name BackForceFeeder
        // answers to with the real board's reply.
        m_items.push_back({"m2ffb", 0, false, true});
        m_bits.push_back(kDriveHex);
    }
    m_first_game_item = m_items.size();
    for (const rom::OutputSpec& spec : game_outputs) {
        m_items.push_back({spec.name, 0, true});
        m_bits.push_back(kGameOutput);
        m_game_outputs.push_back({spec});
    }
    for (Backend* backend : {m_network.get(), m_windows.get()}) {
        if (backend != nullptr) {
            backend->start(m_game, m_items);
        }
    }
}

void Outputs::build_items(const std::string& family)
{
    const auto add = [this](std::string name, int bit) {
        m_items.push_back({std::move(name), 0, true});
        m_bits.push_back(bit);
    };
    for (int i = 0; i < 6; ++i) {
        add("lamp" + std::to_string(i), i + 2);
    }
    add("LampStart", 2);
    for (const NamedBit& named : family_lamps(family)) {
        add(named.name, named.bit);
    }
    add("RawLamps", kRawLamps);
    add("RawDrive", kRawDrive);
}

void Outputs::set_paused(bool paused)
{
    if (paused == m_paused) {
        return;
    }
    m_paused = paused;
    if (m_game.empty()) {
        return;
    }
    for (Backend* backend : {m_network.get(), m_windows.get()}) {
        if (backend != nullptr) {
            backend->paused(paused);
        }
    }
}

void Outputs::update(u8 lamp_latch, std::span<const u8> drive_writes,
                     std::span<const u8> work_ram)
{
    if (m_game.empty() || (m_network == nullptr && m_windows == nullptr)) {
        return;
    }
    update_game_outputs(lamp_latch, work_ram);
    for (usize i = 0; i < m_items.size(); ++i) {
        if (m_bits[i] >= 0) {
            set(i, (lamp_latch >> m_bits[i]) & 1);
        } else if (m_bits[i] == kRawLamps) {
            set(i, lamp_latch);
        } else if (m_bits[i] == kGameOutput) {
            set(i, m_game_outputs[i - m_first_game_item].value);
        } else {
            // Every byte in order: a force command can be followed by its
            // parameters within one frame.
            for (const u8 value : drive_writes) {
                set(i, value);
            }
        }
    }
}

void Outputs::update_game_outputs(u8 lamp_latch, std::span<const u8> work_ram)
{
    using Kind = rom::OutputSpec::Kind;

    for (GameOutput& output : m_game_outputs) {
        const rom::OutputSpec& spec = output.spec;
        output.previous = output.value;
        output.was_live = output.live;

        switch (spec.kind) {
        case Kind::Lamp:
            output.live  = true;
            output.value = (lamp_latch >> spec.bit) & 1;
            break;

        case Kind::Value: {
            output.live = !work_ram.empty();
            for (const rom::OutputSpec::When& when : spec.when) {
                const u32 byte = ram_byte(work_ram, when.address) & when.mask;
                output.live    = output.live && (when.any ? byte != 0 : byte == when.value);
            }
            output.value = output.live ? ram_value(work_ram, spec) : 0;
            break;
        }

        case Kind::Flag:
            output.live  = true;
            output.value = m_game_outputs[spec.source].value > 0 ? 1 : 0;
            break;

        case Kind::Pulse: {
            // The source comes earlier in the list, so it already holds this
            // frame's value. A value that appears or vanishes with its
            // condition (a player joining, a game ending) is not an edge.
            const GameOutput& source = m_game_outputs[spec.source];
            if (source.live && source.was_live
                && (spec.rising ? source.value > source.previous
                                : source.value < source.previous)) {
                output.pending = true;
            }
            // One frame at zero between two pulses, or a reader that only sees
            // changes would take a burst for a single shot.
            if (output.pending && output.on_frames == 0 && output.off_frames == 0) {
                output.pending   = false;
                output.on_frames = spec.frames;
            }
            output.live  = true;
            output.value = output.on_frames > 0 ? 1 : 0;
            if (output.on_frames > 0) {
                if (--output.on_frames == 0) {
                    output.off_frames = 1;
                }
            } else if (output.off_frames > 0) {
                --output.off_frames;
            }
            break;
        }
        }
    }
}

void Outputs::set(usize index, s32 value)
{
    Item& item = m_items[index];
    if (item.known && item.value == value) {
        return;
    }
    item.value = value;
    item.known = true;
    for (Backend* backend : {m_network.get(), m_windows.get()}) {
        if (backend != nullptr) {
            backend->changed(index, item);
        }
    }
}

void Outputs::poll()
{
    if (m_network != nullptr) {
        m_network->poll(m_game, m_items, m_paused);
    }
}

Outputs::NetworkStatus Outputs::network_status() const
{
    return m_network != nullptr ? m_network->status() : NetworkStatus{};
}

std::optional<u8> Outputs::drive_reply() const
{
    return m_network != nullptr ? m_network->drive_reply() : std::nullopt;
}

std::vector<Outputs::Listed> Outputs::listing() const
{
    std::vector<Listed> rows;
    if (m_game.empty()) {
        return rows;
    }
    // Values go out only through a backend that is on.
    const bool sending = m_network != nullptr || m_windows != nullptr;
    const auto shown   = [&](const Item& item) -> std::string {
        if (!sending || !item.known) {
            return "-";
        }
        return item.hex ? hex_byte(static_cast<u8>(item.value)) : std::to_string(item.value);
    };
    const auto text = [](const char* msgid, int arg) {
        return arg >= 0 ? i18n::trf(msgid, arg) : std::string(i18n::tr(msgid));
    };
    // What a latch bit is for in this game: the name its own outputs give it,
    // its family's, the start lamp, or else only which bit it is.
    const auto bit_meaning = [&](int bit) {
        int arg = -1;
        for (const GameOutput& output : m_game_outputs) {
            if (output.spec.kind == rom::OutputSpec::Kind::Lamp && output.spec.bit == bit) {
                const char* msgid = game_output_meaning(output.spec, &arg);
                return text(msgid, arg);
            }
        }
        for (const NamedBit& named : family_lamps(m_family)) {
            if (named.bit == bit) {
                const char* msgid = lamp_meaning(named.name, bit, &arg);
                return text(msgid, arg);
            }
        }
        return bit == 2 ? text(N_("Start button lamp"), -1) : text(N_("Lamp latch bit %d"), bit);
    };

    rows.push_back({"mame_start", i18n::tr("text"),
                    i18n::tr("The game's name, the first line a client gets"),
                    sending ? m_game : "-"});
    rows.push_back({"pause", i18n::tr("0 or 1"), i18n::tr("1 while the emulation is paused"),
                    sending ? (m_paused ? "1" : "0") : "-"});
    // The latch, then its bits: MAME's names, then this game's own for them.
    for (usize i = 0; i < m_items.size(); ++i) {
        if (m_bits[i] == kRawLamps) {
            rows.push_back({m_items[i].name, i18n::tr("byte, 0 to 255"),
                            i18n::tr("The lamp latch: lamp0 to lamp5 on bits 2 to 7"),
                            shown(m_items[i])});
        }
    }
    for (usize i = 0; i < m_items.size(); ++i) {
        const int bit = m_bits[i];
        if (bit < 0) {
            continue;
        }
        const Item& item    = m_items[i];
        std::string meaning = bit_meaning(bit);
        if (item.name != "lamp" + std::to_string(bit - 2)) {
            meaning = i18n::trf("%s (lamp%d)", meaning.c_str(), bit - 2);
        }
        rows.push_back({item.name, i18n::tr("0 or 1"), std::move(meaning), shown(item)});
    }
    // The drive board's bytes, from a game that has one, and the set's own.
    for (usize i = 0; i < m_items.size(); ++i) {
        const Item& item = m_items[i];
        const int   bit  = m_bits[i];
        if (bit == kRawDrive && m_drive_board) {
            rows.push_back({item.name, i18n::tr("byte, 0 to 255, one line each"),
                            i18n::tr("Each byte the game writes to its drive board"), shown(item)});
        } else if (bit == kDriveHex) {
            rows.push_back({item.name, i18n::tr("byte in hex, 00 to FF, one line each"),
                            i18n::tr("The drive board's bytes again, in hex, for BackForceFeeder"),
                            shown(item)});
            // What a tool answers it with, sent the other way.
            const std::optional<u8> reply = drive_reply();
            rows.push_back({"m2ffb_rx", i18n::tr("byte in hex, 00 to FF"),
                            i18n::tr("Received from BackForceFeeder: the real drive board's reply, "
                                     "which the game then reads"),
                            reply ? hex_byte(*reply) : "-"});
        } else if (bit == kGameOutput) {
            const rom::OutputSpec& spec = m_game_outputs[i - m_first_game_item].spec;
            int         arg   = -1;
            const char* msgid = game_output_meaning(spec, &arg);
            rows.push_back({item.name,
                            i18n::tr(spec.kind == rom::OutputSpec::Kind::Value ? "number" : "0 or 1"),
                            text(msgid, arg), shown(item)});
        }
    }
    rows.push_back({"mame_stop", "1", i18n::tr("When the game exits"), "-"});
    return rows;
}

}  // namespace sm2::osd
