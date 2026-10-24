//  ____  __  __  ____         _____ __  __ _   _
// / ___||  \/  ||___ \       | ____|  \/  | | | |
// \___ \| |\/| |  __) |_____ |  _| | |\/| | | | |
//  ___) | |  | | / __/|_____|| |___| |  | | |_| |
// |____/|_|  |_||_____|      |_____|_|  |_|\___/
//
// A Sega Model 2 arcade emulator.
// Copyright (c) 2025+ Daniel Martin (dmanlfc)
// SPDX-License-Identifier: BSD-3-Clause
//
// This header must not be removed. The source files in this project may not be
// used to contribute to commercial projects or for monetary gain without the
// express written permission of the author.
//
// The protocol: one command a line on stdin, one answer on stdout. Once the
// game is loaded: "ready GAME W H" (the picture's size). Supermodel's agent
// mode answers the same commands the same way, so one client drives both.
//
//   step N STEER ACCEL BRAKE IN0 IN1
//       Hold the controls and run N frames. STEER -1 (left) to 1 (right),
//       ACCEL and BRAKE 0 to 1, IN0 and IN1 the two button ports in hex, active
//       low (FF: nothing pressed). Answer: "ok FRAME" then every byte written to
//       the drive board meanwhile, in hex, on the same line.
//   step [N]
//       The same with the holds as they are (one frame without N).
//   set INPUT VALUE / setraw INPUT RAW / release INPUT|all
//       Hold an input, by its name in `inputs`: an axis -1..1 the way a real
//       wheel turns, trigonometrically, +1 at its left (low) end and -1 at its
//       right: Steering 1 fully left (as BackForceFeeder and Supermodel have
//       it; the positional step's STEER is the other way). Accelerator, Brake
//       and the other analogue controls 0..1, a switch 0 or 1, Gear 0
//       (neutral) to 4; RAW the byte the game reads (a port, a channel, the
//       gear bits), decimal or 0x hex. Held from the next frame on, over the
//       controllers with --agent-device, else over the controls at rest.
//       Answer: "ok", or "err ...".
//   get INPUT
//       What the game reads now: "value RAW" (a switch: 1 pressed).
//   inputs
//       "inputs N", then N lines "NAME TYPE", TYPE axis, analog, switch or other.
//   ffb
//       The force feedback's commands since the last `ffb`: "ffb N", then N
//       lines "FRAME Steering EFFECT FORCE", EFFECT constant, vibrate,
//       selfcenter or stop, FORCE -1..1 of the device's range. A constant
//       force > 0 pushes left, towards Steering +1, < 0 right: the
//       trigonometric way of a real wheel and BackForceFeeder's, also
//       DirectInput's, so vJoy gets a magnitude of FORCE * 10000. A vibrate's
//       FORCE is its magnitude. Recorded with or without a device (see
//       AgentOptions).
//   servo on|off / servo
//       Record the emulated drive board's ports, or read them since the last
//       `servo`: "servo N", then N lines "FRAME NMI PORT VALUE", PORT and VALUE
//       in hex, NMI the board's interrupts so far. As the real board's program
//       has them: the command it is shown (28 on EPR-18261, 27 on Sega Rally's
//       and Daytona's), its motor outputs (2A and 2E on EPR-18261, each power
//       bit-reversed and complemented; 46 on Daytona's, complemented, bit 6 the
//       way up, bit 7 down), one write a port at every interrupt: the agent
//       runs the board models one interrupt at a time. Sega Rally's board has
//       no model: its commands only.
//   settle [MS]
//       With --agent-device: how long a step waits before it reads the
//       controllers, so that an axis set just before is read in its first
//       frame, as in Supermodel (2 ms unless set). Answer: "settle MS".
//   frame
//       "frame N", the frames run.
//   obs W H
//       The picture, scaled to W x H. Answer: "obs W H", a new line, then
//       W * H * 3 bytes, RGB, row by row.
//   bands N
//       The picture drawn in N bands of rows, a thread each (up to 4 unless
//       set): 1 when many agents run at once. Answer: "ok".
//   perf [N]
//       Run N frames (600) with the holds, timing the cores. Answer: "perf
//       frames N wall MS i960 MS copro MS sound MS ms/frame".
//   shot PATH
//       The picture as a BMP, full size. Answer: "ok" or "err".
//   ram OFFSET LENGTH
//       The host CPU's work RAM (hex offset and length). Answer: "ram LENGTH",
//       a new line, then the bytes.
//   mem ADDRESS LENGTH
//       The same by the i960's address (hex): the work RAM at 00500000 or the
//       scratch RAM at 00200000. Answer: "mem LENGTH" (0 elsewhere), the bytes.
//   tel [PATH,PATH...]
//       The telemetry, one JSON line (see src/telemetry), or "tel none". With
//       PATHs, only those keys (dotted, "player.speed", "others.x"), as the
//       telemetry server's `fields`.
//   save PATH / load PATH
//       A save state, the drive board model included. Answer: "ok" or "err".
//   nvram
//       The game's backup RAM and EEPROM written to the saves folder now, as
//       a window writes them on exit; the agent never does by itself.
//       Answer: "ok".
//   quit
//       Answer: "bye".
#include "agent.h"

#include "core/log.h"
#include "core/types.h"
#include "hw/model2_machine_base.h"
#include "hw/model2_softrender.h"
#include "hw/save_state_io.h"
#include "hw/sound_board.h"
#include "osd/audio.h"
#include "osd/frame_pacer.h"
#include "osd/input.h"
#include "rom/game.h"
#include "telemetry/telemetry.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace sm2 {
namespace {

constexpr double kMachineHz = 57.5245;

/// --agent-show: the game in a window, its sound, at its own pace. It only
/// looks on: the emulation is the agent's, each frame it runs drawn (the soft
/// renderer `obs` draws with; skipped while the frames run late) and shown,
/// the sound board's samples played, the frame then held to the machine's
/// rate.
class Show {
public:
    Show() = default;
    Show(const Show&)            = delete;
    Show& operator=(const Show&) = delete;

    ~Show()
    {
        if (m_window != nullptr) {
            SDL_DestroyWindow(m_window);
        }
    }

    [[nodiscard]] bool open(const std::string& title, hw::SoundBoard* sound, bool throttle, u32 volume)
    {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            SM2_ERROR("--agent-show: no video: %s", SDL_GetError());
            return false;
        }
        // The window's own surface, blitted to: SDL's 2D renderer is not built
        // here (the GPU backends present the windowed emulator).
        m_window = SDL_CreateWindow(title.c_str(), kWidth * 2, kHeight * 2, SDL_WINDOW_RESIZABLE);
        if (m_window == nullptr || SDL_GetWindowSurface(m_window) == nullptr) {
            SM2_ERROR("--agent-show: no window: %s", SDL_GetError());
            return false;
        }
        if (sound != nullptr && !m_audio.init(sound->sample_rate())) {
            SM2_WARN("--agent-show: no sound");
        }
        m_volume = volume;
        m_pacer.set_throttled(throttle);
        m_pacer.start(static_cast<u64>(1e9 / kMachineHz));
        return true;
    }

    /// A frame's sound, before the board's samples are cleared.
    void sound(std::span<const s16> samples)
    {
        if (m_volume == 100) {
            m_audio.submit(samples);
            return;
        }
        m_scaled.resize(samples.size());
        for (usize i = 0; i < samples.size(); ++i) {
            const s32 v = static_cast<s32>(samples[i]) * static_cast<s32>(m_volume) / 100;
            m_scaled[i] = static_cast<s16>(std::clamp(v, -32768, 32767));
        }
        m_audio.submit(m_scaled);
    }

    /// Whether to draw this frame: always when unthrottled; at the machine's
    /// pace, not after a frame the pacer found late (it had nothing left to
    /// wait), so that the picture skips while the game and its sound keep time.
    [[nodiscard]] bool drawing() const { return !m_pacer.throttled() || !m_late; }

    /// A frame's picture shown: each pixel k times where the window holds k
    /// times the machine's picture, else stretched 4:3.
    void picture(std::span<const u32> pixels)
    {
        // The surface changes with the window's size: asked for every frame.
        SDL_Surface* window = SDL_GetWindowSurface(m_window);
        if (window != nullptr) {
            if (window != m_surface || window->w != m_surface_w || window->h != m_surface_h) {
                SDL_FillSurfaceRect(window, nullptr, 0);  // the borders, once a size
                m_surface   = window;
                m_surface_w = window->w;
                m_surface_h = window->h;
            }
            const int  k    = std::min(window->w / kWidth, window->h / kHeight);
            const bool xrgb = window->format == SDL_PIXELFORMAT_XRGB8888 || window->format == SDL_PIXELFORMAT_ARGB8888;
            if (k >= 1 && xrgb && !SDL_MUSTLOCK(window)) {
                // Each pixel k times, red and blue swapped on the way: the
                // machine's pixels as they are, at a fraction of a filtered
                // blit's cost.
                const int x0 = (window->w - kWidth * k) / 2, y0 = (window->h - kHeight * k) / 2;
                m_row.resize(static_cast<usize>(kWidth) * k);
                for (int y = 0; y < kHeight; ++y) {
                    const u32* in = pixels.data() + static_cast<usize>(y) * kWidth;
                    for (int x = 0; x < kWidth; ++x) {
                        const u32 p = in[x];
                        const u32 q = ((p & 0xff) << 16) | (p & 0xff00) | ((p >> 16) & 0xff);
                        for (int i = 0; i < k; ++i) {
                            m_row[static_cast<usize>(x) * k + i] = q;
                        }
                    }
                    for (int i = 0; i < k; ++i) {
                        u8* out = static_cast<u8*>(window->pixels)
                                + static_cast<usize>(y0 + y * k + i) * static_cast<usize>(window->pitch);
                        std::memcpy(reinterpret_cast<u32*>(out) + x0, m_row.data(), m_row.size() * sizeof(u32));
                    }
                }
            } else {
                // Smaller than the machine's picture, or another format: SDL's
                // stretch, 4:3, from the picture in the window's format.
                m_converted.resize(pixels.size());
                SDL_ConvertPixels(kWidth, kHeight, SDL_PIXELFORMAT_XBGR8888, pixels.data(), kWidth * 4,
                                  window->format, m_converted.data(), kWidth * 4);
                SDL_Surface* frame =
                    SDL_CreateSurfaceFrom(kWidth, kHeight, window->format, m_converted.data(), kWidth * 4);
                if (frame != nullptr) {
                    int w = window->w, h = w * 3 / 4;
                    if (h > window->h) {
                        h = window->h;
                        w = h * 4 / 3;
                    }
                    const SDL_Rect at = {(window->w - w) / 2, (window->h - h) / 2, w, h};
                    SDL_BlitSurfaceScaled(frame, nullptr, window, &at, SDL_SCALEMODE_NEAREST);
                    SDL_DestroySurface(frame);
                }
            }
            SDL_UpdateWindowSurface(m_window);
        }
    }

    /// The frame held to the machine's rate.
    void pace()
    {
        const auto before = std::chrono::steady_clock::now();
        static_cast<void>(m_pacer.wait());
        m_late = std::chrono::steady_clock::now() - before < std::chrono::microseconds(500);
    }

    /// An event of the window's (closing it ends the agent).
    void event(const SDL_Event& e)
    {
        if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            m_closed = true;
        }
    }

    /// The window's events, where no device's loop reads them.
    void pump()
    {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            event(e);
        }
    }

    /// After a load or a pause of the agent's: the pace from now.
    void resync() { m_pacer.resync(); }

    [[nodiscard]] bool closed() const { return m_closed; }

private:
    static constexpr int kWidth  = static_cast<int>(hw::SoftRenderer::kWidth);
    static constexpr int kHeight = static_cast<int>(hw::SoftRenderer::kHeight);

    SDL_Window*      m_window = nullptr;
    osd::Audio       m_audio;
    osd::FramePacer  m_pacer;
    std::vector<s16> m_scaled;
    std::vector<u32> m_converted;
    std::vector<u32> m_row;
    SDL_Surface*     m_surface   = nullptr;
    int              m_surface_w = 0;
    int              m_surface_h = 0;
    bool             m_late      = false;
    u32              m_volume = 100;
    bool             m_closed = false;
};

/// A command the force feedback gives the wheel.
struct FfbRecord {
    u64         frame;
    const char* effect;
    float       force;  ///< -1..1 of the device's range.
};

/// A port the emulated drive board writes.
struct BoardRecord {
    u64 frame;
    u64 nmi;
    u8  port;
    u8  value;
};

/// EPR-18261's power byte: the power bit-reversed and complemented.
u8 indy_port_byte(int power)
{
    const u8 v = static_cast<u8>(power);
    u8       r = 0;
    for (int i = 0; i < 8; ++i) {
        r = static_cast<u8>((r << 1) | ((v >> i) & 1));
    }
    return static_cast<u8>(~r);
}

/// A control's value on its channel, `fraction` 0..1 of its travel.
u8 channel_value(const rom::AnalogChannel& channel, float fraction)
{
    fraction         = std::clamp(fraction, 0.0f, 1.0f);
    const float span = static_cast<float>(channel.maximum - channel.minimum);
    const float v    = channel.reverse ? static_cast<float>(channel.maximum) - fraction * span
                                       : static_cast<float>(channel.minimum) + fraction * span;
    return static_cast<u8>(v + 0.5f);
}

/// The fraction 0..1 of its travel a channel's value stands for.
float channel_fraction(const rom::AnalogChannel& channel, u8 value)
{
    const float span = static_cast<float>(std::max(1, channel.maximum - channel.minimum));
    const float f    = (static_cast<float>(value) - static_cast<float>(channel.minimum)) / span;
    return std::clamp(channel.reverse ? 1.0f - f : f, 0.0f, 1.0f);
}

/// An input the agent can hold, by name.
struct Control {
    enum class Kind : u8 { Axis, Analog, Switch, Port, Gear };
    std::string name;
    Kind        kind    = Kind::Switch;
    int         channel = -1;  ///< Axis, Analog: the mux channel.
    int         port    = 0;   ///< Switch, Port: 0 IN0, 1 IN1, 2 IN2.
    u8          mask    = 0;   ///< Switch: its bit, active low.
    std::optional<int> held;   ///< The raw value held, if any.
};

const char* kind_name(Control::Kind kind)
{
    switch (kind) {
        case Control::Kind::Axis:   return "axis";
        case Control::Kind::Analog: return "analog";
        case Control::Kind::Switch: return "switch";
        default:                    return "other";
    }
}

/// The inputs of this game, Supermodel's names where it has them.
std::vector<Control> game_controls(const rom::GameSpec& game)
{
    std::vector<Control> out;
    const auto analog_name = [](rom::AnalogControl c) -> const char* {
        switch (c) {
            case rom::AnalogControl::Steer:    return "Steering";
            case rom::AnalogControl::Accel:    return "Accelerator";
            case rom::AnalogControl::Brake:    return "Brake";
            case rom::AnalogControl::Throttle: return "Throttle";
            case rom::AnalogControl::Bank:     return "Bank";
            case rom::AnalogControl::Handle:   return "Handle";
            case rom::AnalogControl::StickX:   return "StickX";
            case rom::AnalogControl::StickY:   return "StickY";
            default:                           return nullptr;
        }
    };
    for (int i = 0; i < static_cast<int>(game.analog.size()); ++i) {
        const rom::AnalogControl c    = game.analog[static_cast<usize>(i)].control;
        const char*              name = analog_name(c);
        if (name == nullptr) {
            continue;
        }
        const bool centred = c == rom::AnalogControl::Steer || c == rom::AnalogControl::Bank
                          || c == rom::AnalogControl::Handle || c == rom::AnalogControl::StickX
                          || c == rom::AnalogControl::StickY;
        const bool taken = std::any_of(out.begin(), out.end(),
                                       [name](const Control& k) { return k.name == name; });
        Control k;
        k.name    = taken ? std::string(name) + std::to_string(i) : std::string(name);
        k.kind    = centred ? Control::Kind::Axis : Control::Kind::Analog;
        k.channel = i;
        out.push_back(k);
    }
    for (int i = 0; i < static_cast<int>(game.analog.size()); ++i) {
        Control k;
        k.name    = "Analog" + std::to_string(i);
        k.kind    = Control::Kind::Analog;
        k.channel = i;
        out.push_back(k);
    }
    const auto add_switch = [&out](const std::string& name, int port, u8 mask) {
        Control k;
        k.name = name;
        k.port = port;
        k.mask = mask;
        out.push_back(k);
    };
    add_switch("Coin1", 0, 0x01);
    add_switch("Coin2", 0, 0x02);
    add_switch("Test", 0, 0x04);
    add_switch("Service", 0, 0x08);
    add_switch("Start1", 0, game.start1_bit);
    if (game.start1_bit != 0x20) {
        add_switch("Start2", 0, 0x20);
    }
    if (game.has_steering()) {
        const int vr = game.vr_buttons_declared ? game.vr_button_count : 4;
        for (int i = 0; i < vr; ++i) {
            const auto& [port, mask] = game.wheel_button_bits[static_cast<usize>(i)];
            add_switch("VR" + std::to_string(i + 1), port, mask);
        }
    }
    if (game.shift_buttons) {
        add_switch("ShiftUp", 1, 0x10);
        add_switch("ShiftDown", 1, 0x20);
    }
    for (int i = 0; i < 4; ++i) {
        add_switch("Button" + std::to_string(i + 1), 1, static_cast<u8>(1u << i));
    }
    add_switch("Down", 1, 0x10);
    add_switch("Up", 1, 0x20);
    add_switch("Right", 1, 0x40);
    add_switch("Left", 1, 0x80);
    if (game.gearbox) {
        Control k;
        k.name = "Gear";
        k.kind = Control::Kind::Gear;
        out.push_back(k);
    }
    for (int i = 0; i < 3; ++i) {
        Control k;
        k.name = "IN" + std::to_string(i);
        k.kind = Control::Kind::Port;
        k.port = i;
        out.push_back(k);
    }
    return out;
}

u8& port_of(hw::Inputs& in, int port)
{
    return port == 0 ? in.in0 : port == 1 ? in.in1 : in.in2;
}

/// The holds over `in`: whole ports first, then switches, channels, the gear.
void apply_holds(const std::vector<Control>& controls, hw::Inputs& in)
{
    for (const Control& k : controls) {
        if (k.held && k.kind == Control::Kind::Port) {
            port_of(in, k.port) = static_cast<u8>(*k.held);
        }
    }
    for (const Control& k : controls) {
        if (!k.held) {
            continue;
        }
        switch (k.kind) {
            case Control::Kind::Switch:
                if (*k.held != 0) {
                    port_of(in, k.port) &= static_cast<u8>(~k.mask);
                } else {
                    port_of(in, k.port) |= k.mask;
                }
                break;
            case Control::Kind::Axis:
            case Control::Kind::Analog:
                in.analog[static_cast<usize>(k.channel)] = static_cast<u8>(*k.held);
                break;
            case Control::Kind::Gear: in.gears = static_cast<u8>(*k.held); break;
            default: break;
        }
    }
}

/// A raw value from "123" or "0x7B".
bool parse_raw(const char* text, int* out)
{
    char*      end = nullptr;
    const long v   = std::strtol(text, &end, 0);
    if (end == text) {
        return false;
    }
    *out = static_cast<int>(v);
    return true;
}

bool write_bmp(const char* path, const std::vector<u32>& frame, u32 w, u32 h)
{
    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr) {
        return false;
    }
    const u32 row  = (w * 3 + 3) & ~3u;
    const u32 size = 54 + row * h;
    u8        header[54] = {'B', 'M'};
    const auto put32 = [&header](int at, u32 v) {
        for (int i = 0; i < 4; ++i) {
            header[at + i] = static_cast<u8>(v >> (8 * i));
        }
    };
    put32(2, size);
    put32(10, 54);
    put32(14, 40);
    put32(18, w);
    put32(22, h);
    header[26] = 1;
    header[28] = 24;
    put32(34, row * h);
    std::fwrite(header, 1, sizeof(header), f);
    std::vector<u8> line(row, 0);
    for (u32 y = 0; y < h; ++y) {
        const u32* src = &frame[static_cast<usize>(h - 1 - y) * w];  // bottom row first
        for (u32 x = 0; x < w; ++x) {
            line[x * 3 + 0] = static_cast<u8>(src[x] >> 16);
            line[x * 3 + 1] = static_cast<u8>(src[x] >> 8);
            line[x * 3 + 2] = static_cast<u8>(src[x]);
        }
        std::fwrite(line.data(), 1, row, f);
    }
    return std::fclose(f) == 0;
}

}  // namespace

int run_agent(hw::Model2MachineBase& machine, hw::SoundBoard* sound, const rom::GameSpec& game,
              const AgentOptions& options)
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    hw::SoftRenderer renderer;
    std::vector<u32> frame(static_cast<usize>(hw::SoftRenderer::kWidth) * hw::SoftRenderer::kHeight);
    std::vector<u8>  pixels;
    u64              frames = 0;
    std::unique_ptr<telemetry::GameTelemetry> decoder = telemetry::make_telemetry(game);
    telemetry::Frame tel;
    // The telemetry follows every frame run, so that its rates and
    // accelerations are right whenever `tel` asks.
    bool tel_ok = false;
    u64  tel_at = ~0ull;
    const auto telemetry_update = [&] {
        const hw::Inputs& used = machine.inputs();
        tel_ok = decoder && decoder->update({machine.work_ram(), machine.scratch_ram()}, frames, used.analog,
                                            used.in0, used.in1, tel);
        tel_at = frames;
    };

    std::vector<Control> controls = game_controls(game);
    const auto find = [&controls](const char* name) -> Control* {
        for (Control& k : controls) {
            if (k.name == name) {
                return &k;
            }
        }
        return nullptr;
    };

    // The controls at rest, as a cabinet nobody touches: what a hold leaves.
    hw::Inputs& inputs = machine.inputs();
    for (usize i = 0; i < game.analog.size(); ++i) {
        const rom::AnalogChannel& channel = game.analog[i];
        switch (channel.control) {
            case rom::AnalogControl::Steer: inputs.analog[i] = channel_value(channel, 0.5f); break;
            case rom::AnalogControl::Accel:
            case rom::AnalogControl::Brake: inputs.analog[i] = channel_value(channel, 0.0f); break;
            default: break;
        }
    }
    const hw::Inputs rest = inputs;

    // The force feedback and the drive board model, on the virtual wheel
    // unless a device drives them; in save states as in a window.
    osd::Input input;
    if (options.device) {
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        // DirectInput, read when asked, rather than Raw Input's messages: an
        // axis set before a step is read in that frame, as in Supermodel.
        SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "0");
        if (!SDL_Init(0) || !input.init(options.wheel)) {
            SM2_ERROR("--agent-device: the controllers do not open: %s", SDL_GetError());
            return 1;
        }
    }
    // A virtual wheel is there for its force feedback.
    osd::Input::WheelSettings wheel = options.wheel;
    wheel.ffb = wheel.ffb || !options.device;
    input.set_wheel_settings(wheel);
    // --agent-show: the window and the sound.
    std::unique_ptr<Show> show;
    if (options.show) {
        show = std::make_unique<Show>();
        if (!show->open("sm2-emu agent: " + game.name, sound, options.throttle, options.volume)) {
            return 1;
        }
    }
    std::vector<FfbRecord>   ffb_log;
    std::vector<BoardRecord> board_log;
    bool                     servo   = false;
    int                      settle  = 2;    ///< ms, with a device.
    u64                      nmis    = 0;    ///< The board's interrupts run.
    double                   nmi_acc = 0.0;  ///< And a fraction of the next.
    const bool indy  = game.drive_protocol == rom::DriveProtocol::Indy;
    const auto board = [&](u8 port, u8 value) {
        if (servo && board_log.size() < 4000000) {
            board_log.push_back({frames, nmis, port, value});
        }
    };
    osd::Input::AgentHooks hooks;
    hooks.virtual_wheel = !options.device;
    hooks.ffb           = [&](const char* effect, int value) {
        // Input's level pushes left when positive, as the record has it.
        if (ffb_log.size() < 1000000) {
            ffb_log.push_back({frames, effect, static_cast<float>(value) / 32767.0f});
        }
    };
    // The models' interrupts one at a time, each output journaled: the same
    // outputs at the end of the frame as all of them at once.
    hooks.indy = [&](osd::IndyBoard& b, int position, double n) {
        osd::IndyBoard::Output out = {};
        bool                   ran = false;
        for (nmi_acc += n; nmi_acc >= 1.0; nmi_acc -= 1.0, ran = true) {
            ++nmis;
            out = b.step(position, 1.0);
            board(0x2A, indy_port_byte(out.up));
            board(0x2E, indy_port_byte(out.down));
        }
        return ran ? out : b.step(position, 0.0);
    };
    hooks.daytona = [&](osd::DaytonaBoard& b, int counts, double n, int breakaway) {
        osd::DaytonaBoard::Output out = {};
        bool                      ran = false;
        for (nmi_acc += n; nmi_acc >= 1.0; nmi_acc -= 1.0, ran = true) {
            ++nmis;
            out = b.step(counts, 1.0, breakaway);
            const u8 way = out.way == osd::DaytonaBoard::Way::Up     ? 0x40
                         : out.way == osd::DaytonaBoard::Way::Down ? 0x80
                                                                   : 0x00;
            board(0x46, static_cast<u8>(~(way | (out.power & 0x3f))));
        }
        return ran ? out : b.step(counts, 0.0, breakaway);
    };
    input.set_agent_hooks(&hooks);
    hw::set_host_state([&input](Archive& ar) { input.serialize_drive_board(ar); });

    const int steer_channel = [&game] {
        for (usize i = 0; i < game.analog.size(); ++i) {
            if (game.analog[i].control == rom::AnalogControl::Steer) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }();

    std::printf("ready %s %u %u\n", game.name.c_str(), hw::SoftRenderer::kWidth,
                hw::SoftRenderer::kHeight);
    std::fflush(stdout);

    char line[1024];
    while (std::fgets(line, sizeof(line), stdin) != nullptr) {
        if (show && show->closed()) {
            break;  // the window closed: the agent with it
        }
        char command[16] = {};
        if (std::sscanf(line, "%15s", command) != 1) {
            continue;
        }
        if (std::strcmp(command, "quit") == 0) {
            std::printf("bye\n");
            std::fflush(stdout);
            break;
        }
        if (std::strcmp(command, "step") == 0) {
            unsigned n = 1, in0 = 0xff, in1 = 0xff;
            float    steer = 0.0f, accel = 0.0f, brake = 0.0f;
            const int fields =
                std::sscanf(line, "%*s %u %f %f %f %x %x", &n, &steer, &accel, &brake, &in0, &in1);
            if (fields >= 2) {
                // The positional form: these controls held from now on.
                for (Control& k : controls) {
                    if (k.kind != Control::Kind::Axis && k.kind != Control::Kind::Analog) {
                        continue;
                    }
                    const rom::AnalogChannel& channel = game.analog[static_cast<usize>(k.channel)];
                    if (k.name.rfind("Analog", 0) == 0) {
                        continue;
                    }
                    const float fraction = channel.control == rom::AnalogControl::Steer ? 0.5f + 0.5f * steer
                                         : channel.control == rom::AnalogControl::Accel ? accel
                                         : channel.control == rom::AnalogControl::Brake ? brake
                                                                                        : -1.0f;
                    if (fraction >= 0.0f) {
                        k.held = channel_value(channel, fraction);
                    }
                }
                find("IN0")->held = static_cast<int>(in0 & 0xff);
                find("IN1")->held = static_cast<int>(in1 & 0xff);
            }
            std::string drive;
            if (options.device && settle > 0) {
                // vJoy's report reaches DirectInput a moment after the axis is set.
                SDL_Delay(static_cast<u32>(settle));
            }
            for (unsigned i = 0; i < n; ++i) {
                if (options.device) {
                    SDL_UpdateJoysticks();
                    SDL_Event event;
                    while (SDL_PollEvent(&event)) {
                        input.handle_event(event);
                        if (show) {
                            show->event(event);
                        }
                    }
                    input.poll(&inputs, game);
                } else {
                    inputs = rest;
                }
                apply_holds(controls, inputs);
                if (steer_channel >= 0) {
                    const rom::AnalogChannel& channel = game.analog[static_cast<usize>(steer_channel)];
                    hooks.steer_value = inputs.analog[static_cast<usize>(steer_channel)];
                    hooks.steer       = 2.0f * channel_fraction(channel, hooks.steer_value) - 1.0f;
                }
                machine.run_frame();
                ++frames;
                if (decoder) {
                    telemetry_update();
                }
                if (sound != nullptr) {
                    if (show) {
                        show->sound(sound->pending_samples());
                    }
                    sound->clear_pending_samples();
                }
                // Kept in a local: a range-for over the view of the temporary
                // would read it after its end.
                const auto writes = machine.take_drive_board_writes();
                for (const u8 b : writes.view()) {
                    char hex[4];
                    std::snprintf(hex, sizeof(hex), " %02X", b);
                    drive += hex;
                }
                // The commands are shown to the board, then it runs the frame's
                // interrupts (the models' hooks count them; Sega Rally's has none).
                for (const u8 b : writes.view()) {
                    board(indy ? 0x28 : 0x27, b);
                }
                input.update_drive_board(game, writes.view());
                input.update_force_feedback(game);
                if (game.drive_protocol == rom::DriveProtocol::Rally) {
                    nmi_acc += 1000.0 / kMachineHz;
                    nmis += static_cast<u64>(nmi_acc);
                    nmi_acc -= static_cast<double>(static_cast<u64>(nmi_acc));
                }
                if (show) {
                    if (!options.device) {
                        show->pump();
                    }
                    if (show->drawing()) {
                        machine.compose_video();
                        renderer.render(machine, machine.render_list(), frame);
                        show->picture(frame);
                    }
                    show->pace();
                }
            }
            std::printf("ok %llu%s\n", static_cast<unsigned long long>(frames), drive.c_str());
        } else if (std::strcmp(command, "set") == 0 || std::strcmp(command, "setraw") == 0) {
            char  name[64] = {}, value[64] = {};
            Control* k = nullptr;
            if (std::sscanf(line, "%*s %63s %63s", name, value) != 2 || (k = find(name)) == nullptr) {
                std::printf("err unknown input\n");
            } else if (command[3] == 'r') {
                int raw = 0;
                if (!parse_raw(value, &raw)) {
                    std::printf("err bad value\n");
                } else {
                    k->held = k->kind == Control::Kind::Switch ? (raw != 0 ? 1 : 0) : (raw & 0xff);
                    std::printf("ok\n");
                }
            } else {
                const float v = std::strtof(value, nullptr);
                const auto channel = [&game, k]() -> const rom::AnalogChannel& {
                    return game.analog[static_cast<usize>(k->channel)];
                };
                // An axis trigonometrically: +1 at its left end.
                const float side = -v;
                switch (k->kind) {
                    case Control::Kind::Axis:   k->held = channel_value(channel(), 0.5f + 0.5f * side); break;
                    case Control::Kind::Analog: k->held = channel_value(channel(), v); break;
                    case Control::Kind::Switch: k->held = v != 0.0f ? 1 : 0; break;
                    case Control::Kind::Gear:
                        k->held = 1 << std::clamp(static_cast<int>(v), 0, 4);
                        break;
                    case Control::Kind::Port: k->held = static_cast<int>(v) & 0xff; break;
                }
                std::printf("ok\n");
            }
        } else if (std::strcmp(command, "release") == 0) {
            char name[64] = {};
            Control* k = nullptr;
            if (std::sscanf(line, "%*s %63s", name) == 1 && std::strcmp(name, "all") == 0) {
                for (Control& c : controls) {
                    c.held.reset();
                }
                std::printf("ok\n");
            } else if ((k = find(name)) != nullptr) {
                k->held.reset();
                std::printf("ok\n");
            } else {
                std::printf("err unknown input\n");
            }
        } else if (std::strcmp(command, "get") == 0) {
            char name[64] = {};
            const Control* k = nullptr;
            if (std::sscanf(line, "%*s %63s", name) != 1 || (k = find(name)) == nullptr) {
                std::printf("err unknown input\n");
            } else {
                int raw = 0;
                const u8 port = port_of(inputs, k->port);
                switch (k->kind) {
                    case Control::Kind::Axis:
                    case Control::Kind::Analog: raw = inputs.analog[static_cast<usize>(k->channel)]; break;
                    case Control::Kind::Switch: raw = (port & k->mask) == 0 ? 1 : 0; break;
                    case Control::Kind::Gear:   raw = inputs.gears; break;
                    case Control::Kind::Port:   raw = port; break;
                }
                std::printf("value %d\n", raw);
            }
        } else if (std::strcmp(command, "inputs") == 0) {
            std::printf("inputs %zu\n", controls.size());
            for (const Control& k : controls) {
                std::printf("%s %s\n", k.name.c_str(), kind_name(k.kind));
            }
        } else if (std::strcmp(command, "ffb") == 0) {
            std::printf("ffb %zu\n", ffb_log.size());
            for (const FfbRecord& r : ffb_log) {
                std::printf("%llu Steering %s %.4f\n", static_cast<unsigned long long>(r.frame),
                            r.effect, static_cast<double>(r.force));
            }
            ffb_log.clear();
        } else if (std::strcmp(command, "servo") == 0) {
            char arg[8] = {};
            if (std::sscanf(line, "%*s %7s", arg) == 1) {
                servo = std::strcmp(arg, "on") == 0;
                std::printf("ok\n");
            } else {
                std::printf("servo %zu\n", board_log.size());
                for (const BoardRecord& r : board_log) {
                    std::printf("%llu %llu %02X %02X\n", static_cast<unsigned long long>(r.frame),
                                static_cast<unsigned long long>(r.nmi), r.port, r.value);
                }
                board_log.clear();
            }
        } else if (std::strcmp(command, "settle") == 0) {
            int ms = settle;
            if (std::sscanf(line, "%*s %d", &ms) == 1) {
                settle = std::clamp(ms, 0, 100);
            }
            std::printf("settle %d\n", settle);
        } else if (std::strcmp(command, "perf") == 0) {
            unsigned n = 600;
            std::sscanf(line, "%*s %u", &n);
            machine.set_core_profiling(true);
            u64        i960 = 0, copro = 0, sound_ns = 0;
            const auto t0   = std::chrono::steady_clock::now();
            for (unsigned k = 0; k < n; ++k) {
                machine.run_frame();
                ++frames;
                i960 += machine.i960_stage_nanoseconds();
                copro += machine.copro_stage_nanoseconds();
                sound_ns += machine.sound_stage_nanoseconds();
            }
            const auto wall = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::steady_clock::now() - t0).count();
            machine.set_core_profiling(false);
            std::printf("perf frames %u wall %.3f i960 %.3f copro %.3f sound %.3f ms/frame\n", n,
                        wall / 1e6 / n, i960 / 1e6 / n, copro / 1e6 / n, sound_ns / 1e6 / n);
        } else if (std::strcmp(command, "bands") == 0) {
            unsigned bands = 1;
            std::sscanf(line, "%*s %u", &bands);
            renderer.set_bands(std::clamp(bands, 1u, 8u));
            std::printf("ok\n");
        } else if (std::strcmp(command, "frame") == 0) {
            std::printf("frame %llu\n", static_cast<unsigned long long>(frames));
        } else if (std::strcmp(command, "obs") == 0) {
            unsigned w = 84, h = 84;
            std::sscanf(line, "%*s %u %u", &w, &h);
            w = std::clamp(w, 1u, hw::SoftRenderer::kWidth);
            h = std::clamp(h, 1u, hw::SoftRenderer::kHeight);
            machine.compose_video();
            renderer.render(machine, machine.render_list(), frame);
            // A box average of the source pixels each output pixel covers.
            pixels.assign(static_cast<usize>(w) * h * 3, 0);
            const u32 sw = hw::SoftRenderer::kWidth, sh = hw::SoftRenderer::kHeight;
            for (u32 y = 0; y < h; ++y) {
                const u32 y0 = y * sh / h, y1 = std::max(y0 + 1, (y + 1) * sh / h);
                for (u32 x = 0; x < w; ++x) {
                    const u32 x0 = x * sw / w, x1 = std::max(x0 + 1, (x + 1) * sw / w);
                    u32 r = 0, g = 0, b = 0, count = 0;
                    for (u32 yy = y0; yy < y1; ++yy) {
                        for (u32 xx = x0; xx < x1; ++xx) {
                            const u32 p = frame[static_cast<usize>(yy) * sw + xx];
                            r += p & 0xff;
                            g += (p >> 8) & 0xff;
                            b += (p >> 16) & 0xff;
                            ++count;
                        }
                    }
                    u8* out = &pixels[(static_cast<usize>(y) * w + x) * 3];
                    out[0]  = static_cast<u8>(r / count);
                    out[1]  = static_cast<u8>(g / count);
                    out[2]  = static_cast<u8>(b / count);
                }
            }
            std::printf("obs %u %u\n", w, h);
            std::fwrite(pixels.data(), 1, pixels.size(), stdout);
        } else if (std::strcmp(command, "shot") == 0) {
            char path[900] = {};
            if (std::sscanf(line, "%*s %899[^\r\n]", path) != 1) {
                std::printf("err\n");
            } else {
                machine.compose_video();
                renderer.render(machine, machine.render_list(), frame);
                std::printf(write_bmp(path, frame, hw::SoftRenderer::kWidth, hw::SoftRenderer::kHeight)
                                ? "ok\n"
                                : "err\n");
            }
        } else if (std::strcmp(command, "nvram") == 0) {
            machine.save_nvram();
            std::printf("ok\n");
        } else if (std::strcmp(command, "ram") == 0) {
            unsigned offset = 0, length = 0;
            std::sscanf(line, "%*s %x %x", &offset, &length);
            const std::span<const u8> ram = machine.work_ram();
            offset = std::min<unsigned>(offset, static_cast<unsigned>(ram.size()));
            length = std::min<unsigned>(length, static_cast<unsigned>(ram.size()) - offset);
            std::printf("ram %u\n", length);
            std::fwrite(ram.data() + offset, 1, length, stdout);
        } else if (std::strcmp(command, "mem") == 0) {
            unsigned address = 0, length = 0;
            std::sscanf(line, "%*s %x %x", &address, &length);
            std::span<const u8> region;
            unsigned            offset = 0;
            if (address >= 0x00500000u) {
                region = machine.work_ram();
                offset = address - 0x00500000u;
            } else if (address >= 0x00200000u) {
                region = machine.scratch_ram();
                offset = address - 0x00200000u;
            }
            offset = std::min<unsigned>(offset, static_cast<unsigned>(region.size()));
            length = std::min<unsigned>(length, static_cast<unsigned>(region.size()) - offset);
            std::printf("mem %u\n", length);
            std::fwrite(region.data() + offset, 1, length, stdout);
        } else if (std::strcmp(command, "tel") == 0) {
            if (tel_at != frames) {
                telemetry_update();  // no frame run since a load
            }
            if (tel_ok) {
                telemetry::Selection selection;
                char                 fields[2048] = {};
                if (std::sscanf(line, "%*s %2047s", fields) == 1) {
                    const std::string_view all(fields);
                    usize                  start = 0;
                    while (start < all.size()) {
                        const usize comma = std::min(all.find(',', start), all.size());
                        if (comma > start) {
                            selection.fields.emplace_back(all.substr(start, comma - start));
                        }
                        start = comma + 1;
                    }
                }
                const std::string json = telemetry::to_json(tel, selection);
                std::fwrite(json.data(), 1, json.size(), stdout);
            } else {
                std::printf("tel none\n");
            }
        } else if (std::strcmp(command, "save") == 0 || std::strcmp(command, "load") == 0) {
            char path[900] = {};
            if (std::sscanf(line, "%*s %899[^\r\n]", path) != 1) {
                std::printf("err\n");
            } else {
                const bool ok = command[0] == 's' ? machine.save_state(path) : machine.load_state(path);
                if (command[0] == 'l') {
                    tel_at = ~0ull;  // another race: the telemetry from it
                    if (decoder) {
                        decoder->forget();
                    }
                    if (show) {
                        show->resync();
                    }
                }
                std::printf(ok ? "ok\n" : "err\n");
            }
        } else {
            std::printf("err unknown command\n");
        }
        std::fflush(stdout);
    }
    input.set_agent_hooks(nullptr);
    hw::set_host_state({});
    return 0;
}

}  // namespace sm2
