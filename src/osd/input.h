// ██╗  ██╗███████╗██╗███████╗██╗  ██╗ ██████╗
// ██║ ██╔╝██╔════╝██║██╔════╝██║  ██║██╔═══██╗
// █████╔╝ █████╗  ██║███████╗███████║██║   ██║
// ██╔═██╗ ██╔══╝  ██║╚════██║██╔══██║██║   ██║
// ██║  ██╗███████╗██║███████║██║  ██║╚██████╔╝
// ╚═╝  ╚═╝╚══════╝╚═╝╚══════╝╚═╝  ╚═╝ ╚═════╝
//            継 承  ( k e i s h ō )
//
// A Sega Model 1, 2 & 3 arcade emulator.
// Copyright (c) 2025+ Daniel Martin (dmanlfc)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This header must not be removed. Keisho is licensed under the GNU General
// Public License, version 3 or later, with additional terms under section 7 of
// that licence. See LICENSE and NOTICE.
//
#pragma once

#include "core/config.h"
#include "core/types.h"
#include "osd/daytona_board.h"
#include "osd/drive_command.h"
#include "osd/indy_board.h"
#include "osd/vr_board.h"
#include "render/backend.h"
#include "rom/game.h"

#include <SDL3/SDL.h>

#include <array>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace sm2 {
class Archive;
}

namespace sm2::hw {
struct Inputs;
}

namespace sm2::osd {

class LightGuns;
class WheelForce;

/// The cabinet's controls, driven by gamepads and the keyboard.
///
/// Inputs are levels, not events, so device state is sampled once per frame,
/// before the frame runs. Gamepads and the keyboard are always both live: a
/// control is pressed if any device presses it.
class Input {
public:
    /// Cabinet player positions. Model 2A wires two.
    static constexpr u32 kPlayers = 2;

    /// Bit assignments of the operator port, from MAME's Model 2 input ports. All
    /// active low: a pressed control pulls its bit to zero.
    static constexpr u8 kCoin1   = 0x01;
    static constexpr u8 kCoin2   = 0x02;
    static constexpr u8 kTest    = 0x04;
    static constexpr u8 kService = 0x08;
    static constexpr u8 kStart1  = 0x10;
    static constexpr u8 kStart2  = 0x20;

    /// Bit assignments of a player port, also active low.
    static constexpr u8 kButton1 = 0x01;
    static constexpr u8 kButton2 = 0x02;
    static constexpr u8 kButton3 = 0x04;
    static constexpr u8 kButton4 = 0x08;
    static constexpr u8 kDown    = 0x10;
    static constexpr u8 kUp      = 0x20;
    static constexpr u8 kRight   = 0x40;
    static constexpr u8 kLeft    = 0x80;

    // -- axis conversion ---------------------------------------------------

    /// Direction bits an analogue stick at (x, y) presses, as an eight-way gate:
    /// a shallow lean means nothing, and a diagonal closes both switches.
    [[nodiscard]] static u8 stick_bits(s16 x, s16 y);

    /// An axis that rests in the middle, such as a steering wheel, mapped onto the
    /// eight-bit value the I/O controller's converter reports.
    [[nodiscard]] static u8 axis_to_centred(s16 value);

    /// An axis that rests at one end, such as a pedal.
    [[nodiscard]] static u8 axis_to_pedal(s16 value);

    Input();
    ~Input();

    Input(const Input&)            = delete;
    Input& operator=(const Input&) = delete;

    /// Feel settings for a force-feedback wheel. `ffb` off leaves the wheel
    /// steering but limp; `strength` is a gain, 0..200 percent, 100 giving the
    /// drive board's full power (63) the wheel's full torque.
    struct WheelSettings {
        bool ffb           = true;
        u32  strength      = 50;
        /// The light centring spring of the Sega Rally and Daytona USA panel,
        /// 0..100 percent of Sega Rally's strongest torque (32) at `strength`;
        /// no other game has one.
        u32  panel_spring  = 20;
        /// Indy 500's family: the drive board's power giving no torque yet, and
        /// the one taken as its full power (see Config::wheel_indy_deadband).
        u32  indy_deadband   = 0;
        u32  indy_full_power = 63;
        /// The drive board's DIP strength, 0..7 (see Config::wheel_drive_dip).
        u32  drive_dip       = 0;
        /// Reverses the force, for a driver that pushes right on the positive
        /// force with which DirectInput, SDL and Linux push left.
        bool ffb_invert    = false;
        /// The device that plays the force, by name; empty: the wheel itself
        /// (see Config::wheel_ffb_device).
        std::string ffb_device;
        /// The joystick read as the wheel, by label; empty: the first that
        /// looks like one (see Config::wheel_device).
        std::string device;
        /// The joystick each pedal is read from, by label; empty: the wheel
        /// (see Config::wheel_accel_device).
        std::string accel_device;
        std::string brake_device;
        u32  steer_degrees = 270;
        u32  lock_degrees  = 240;  ///< physical rotation for full game lock.
        u32  steer_deadzone = 0;   ///< percent of travel about centre read as centre.

        /// Rumble in place of force feedback: the game's impacts, and with
        /// `rumble_engine` an engine hum from the throttle (Daytona streams no
        /// continuous buzz). 0..100 strength.
        bool rumble          = true;
        u32  rumble_strength = 40;
        bool rumble_engine   = true;

        /// Wheel button index per cabinet role, indexed by Config::WheelRole;
        /// -1 unbinds. Set by the GUI, since numbering differs between wheels.
        std::array<s32, Config::kWheelRoleCount> buttons =
            Config{}.wheel_buttons;
        /// A key per role too, by SDL scancode name; empty for none.
        std::array<std::string, Config::kWheelRoleCount> keys{};

        /// An H shifter's lever in none of the gears is in neutral; and how
        /// long, in ms, a sequential shift between two gears shows neutral.
        bool shifter_neutral  = true;
        u32  shift_neutral_ms = 0;

        /// Wheel axis per analogue control, or -1 to auto-detect. Invert marks
        /// a pedal pressed towards the low end; half, a pedal on half its axis.
        s32  steer_axis   = -1;
        s32  accel_axis   = -1;
        s32  brake_axis   = -1;
        bool accel_invert = false;
        bool brake_invert = false;
        bool accel_half   = false;
        bool brake_half   = false;
    };

    /// Start the gamepad subsystem and open whatever is already plugged in.
    /// False only if the subsystem will not start; no gamepad is not an error.
    [[nodiscard]] bool init(const WheelSettings& wheel);

    /// The device path each player's pad must have; set before init().
    void set_pad_devices(const std::array<std::string, 2>& paths) { m_pad_devices = paths; }

    /// Any part of the device path of each player's light gun
    /// (Config::gun_devices); set before init(). Empty keeps the path order.
    void set_gun_devices(const std::array<std::string, 2>& paths) { m_gun_devices = paths; }

    void shutdown();

    /// Offer an SDL event. Consumes connection and disconnection only; everything
    /// else is read as state in poll().
    void handle_event(const SDL_Event& event);

    /// Overwrite the digital and analogue fields of `inputs` from current device
    /// state. Call once per frame, before running the frame. The analogue
    /// channels and lightgun calibration come from the game (`rom::GameSpec::analog`).
    void poll(hw::Inputs* inputs, const rom::GameSpec& game) const;

    /// Digital-only overload, for callers with no game metadata. Leaves every
    /// analogue channel at zero.
    void poll(hw::Inputs* inputs) const;

    /// Replace the wheel settings live, e.g. from a GUI slider.
    void set_wheel_settings(const WheelSettings& wheel);

    /// The emulator's shortcuts as the settings bind them, by SDL key name:
    /// those keys are never bound to a cabinet control nor captured.
    void set_hotkeys(const std::array<std::string, Config::kHotkeyCount>& names);

    /// The connected devices with force feedback, by name, for the settings.
    [[nodiscard]] static std::vector<std::string> force_feedback_devices();

    /// The connected joysticks, by label ("Name", "Name #2"... in SDL's
    /// order), for the settings to choose the wheel from.
    [[nodiscard]] static std::vector<std::string> wheel_devices();

    /// True when a wheel is connected, for the settings UI to show its controls.
    [[nodiscard]] bool wheel_connected() const { return m_wheel.handle != nullptr; }

    /// How many gamepads are open, for the settings UI to report.
    [[nodiscard]] usize pad_count() const { return m_pads.size(); }

    /// The lowest-numbered wheel button currently held, or -1 if none. The
    /// button-binding UI polls this to capture "press the button for X".
    [[nodiscard]] s32 pressed_wheel_button() const;

    /// True once per press of the wheel button bound to the Menu role, for the
    /// main loop to toggle the overlay. Edge-triggered, so a held button fires
    /// once. Returns false when no wheel is connected or Menu is unbound.
    [[nodiscard]] bool menu_button_pressed();

    /// Key capture for the binding UI: begin() notes the keys already held,
    /// so captured_key() returns one pressed since, as an SDL_Scancode, or -1.
    /// The emulator's own keys are never captured.
    void key_capture_begin();
    [[nodiscard]] s32 captured_key();

    /// A key's SDL scancode name, as the config stores it.
    [[nodiscard]] static std::string key_name(s32 key);

    /// A stored key's name on this keyboard's layout, for the settings UI.
    [[nodiscard]] static std::string key_label(const std::string& name);

    /// Emulator shortcuts on a pad's Guide button, laid out as Batocera's
    /// hotkeys: Guide+A menu, Guide+X / Guide+Y quick save / load, Guide+Start
    /// exit (back to the games when there are some), Guide+LB screenshot, and
    /// Guide+d-pad left / right held for rewind / fast-forward. The presses are
    /// one-shot, the holds last while held.
    struct PadShortcuts {
        bool menu         = false;
        bool save         = false;
        bool load         = false;
        bool exit         = false;
        bool screenshot   = false;
        bool fast_forward = false;
        bool rewind       = false;
    };

    /// Read the shortcuts; call once a frame, before poll(). A pad taking part
    /// in one is hidden from the game until its buttons are let go, so the
    /// presses do not also reach the machine. With `enabled` false nothing is
    /// read or hidden, for a host that maps the hotkeys itself.
    [[nodiscard]] PadShortcuts update_pad_shortcuts(bool enabled);

    /// Axis calibration: begin() notes where every axis rests, on the wheel
    /// alone (`any_device` false, for the steering) or on every joystick
    /// connected (the pedals, which may be another device of the base).
    void axis_capture_begin(bool any_device);

    /// What the calibration caught since begin(): the axis that moved
    /// furthest, once past a threshold. False while none has.
    struct CapturedAxis {
        std::string device;           ///< its joystick's label; empty: the wheel.
        int         axis     = -1;
        bool        positive = true;  ///< moved up (a pedal pressed low reports false).
        bool        centred  = false; ///< rested about the centre (a pedal on half its axis).
    };
    [[nodiscard]] bool axis_captured(CapturedAxis* out) const;

    /// Ends the calibration, closing the joysticks it opened.
    void axis_capture_end();

    /// The raw reading of the wheel's steering axis, for the deadzone display.
    /// 0 without a wheel.
    [[nodiscard]] s16 wheel_steer_raw() const;

    /// Decode the drive-board bytes written during the last frame. The last
    /// force command stays in effect until the game sends another.
    void update_drive_board(const rom::GameSpec& game, std::span<const u8> writes);

    /// Saves or restores the drive board model, for save states.
    void serialize_drive_board(Archive& ar);

    /// Forces from a board that computes them itself (Model 3). Constant is
    /// -1..1, positive right; the rest 0..1. Set before update_force_feedback();
    /// while active it replaces decoding the drive-board bytes.
    struct BoardForce {
        bool  active      = false;
        float constant    = 0.0f;
        float self_center = 0.0f;
        float friction    = 0.0f;
        float vibrate     = 0.0f;
    };
    void set_board_force(const BoardForce& force) { m_board_force = force; }

    /// Update the wheel's force from the drive board and the steering position.
    /// Call once per frame after update_drive_board(). Does nothing without a
    /// wheel, without force feedback, or for a title with no steering.
    void update_force_feedback(const rom::GameSpec& game);

    /// Take the force and rumble off the wheel, for a pause: without this the
    /// last frame's force stays on. The next update_force_feedback() resends.
    void release_force_feedback();

    /// The drive board models as at power-up, for a new game: its first bytes
    /// set them up again. Their DIP strength stays.
    void reset_drive_board();

    /// True while a wheel delivers force feedback to a game with a drive board;
    /// then nothing rumbles, the wheel's own rumble and gamepads included.
    [[nodiscard]] bool wheel_ffb_active(const rom::GameSpec& game) const;

    /// Drive gamepad rumble from the drive board; call once per frame.
    void update_pad_rumble(const rom::GameSpec& game);

    /// The agent mode's hooks (src/agent.cpp): a wheel with no device, at the
    /// position it sets, what goes to the wheel, and the board models' steps,
    /// which it runs one interrupt at a time for its journal. Null otherwise.
    struct AgentHooks {
        bool  virtual_wheel = false;
        float steer         = 0.0f;  ///< -1..1 of the game's lock, + right.
        u8    steer_value   = 0x80;  ///< The steering byte the game reads.
        /// What goes to the wheel: constant (the level), vibrate, selfcenter, stop.
        std::function<void(const char* effect, int value)> ffb;
        std::function<IndyBoard::Output(IndyBoard&, int position, double nmis)> indy;
        std::function<DaytonaBoard::Output(DaytonaBoard&, int counts, double ticks, int breakaway)> daytona;
    };
    void set_agent_hooks(AgentHooks* hooks) { m_agent = hooks; }

    /// Names of the gamepads currently open, in player order. An empty string means
    /// that player has no pad.
    [[nodiscard]] std::vector<std::string> gamepad_names() const;

    /// Where each player is aiming, for the crosshair overlay: 0..1 across the
    /// game image (the letterboxed area, not the window). Updated every poll of a
    /// lightgun title.
    struct GunAim {
        bool  active = false;  ///< This player is aiming a gun this frame.
        bool  mouse  = false;  ///< Aimed by the system mouse pointer.
        float x      = 0.5f;
        float y      = 0.5f;
        /// The host tool is calibrating this gun (KEY_CONFIG held); its current
        /// target sits at win_x/win_y, 0..1 across the window rather than the image.
        bool  calibrating = false;
        float win_x       = 0.5f;
        float win_y       = 0.5f;
        bool  idle        = false;
    };
    [[nodiscard]] const std::array<GunAim, kPlayers>& gun_aims() const { return m_gun_aims; }

    /// Number of dedicated evdev light guns opened (0 when built without evdev,
    /// none are plugged in, or the single-mouse fallback is in use).
    [[nodiscard]] usize gun_count() const;

    /// The name of gun `index`, for the settings UI, or empty if out of range.
    [[nodiscard]] std::string gun_name(usize index) const;

    /// Most recent button press on gun `index` (0 if none), consumed on read,
    /// for the settings UI's bind capture. Always 0 without evdev or a gun.
    [[nodiscard]] u16 gun_take_last_pressed(usize index) const;

    /// Recoil settings, pushed from the GUI/config each frame. When on, a gun
    /// with a motor pulses on each trigger pull.
    void set_recoil(bool enabled, u32 strength)
    {
        m_recoil_enabled  = enabled;
        m_recoil_strength = strength;
    }

    void set_pad_aim_centred(bool centred) { m_pad_aim_centred = centred; }

    /// Gamepad rumble settings, pushed from the GUI/config each frame.
    void set_pad_rumble(bool enabled, u32 strength, bool cornering)
    {
        m_pad_rumble_enabled   = enabled;
        m_pad_rumble_strength  = strength;
        m_pad_rumble_cornering = cornering;
    }

    /// Stick gain as a percentage, pushed from the config each frame.
    void set_pad_stick_sensitivity(u32 percent)
    {
        m_pad_stick_response = static_cast<float>(percent) / 100.0f;
    }

    /// Stick deadzone as a percentage of full deflection, pushed each frame.
    void set_pad_stick_deadzone(u32 percent)
    {
        m_pad_stick_dead = 32767.0f * static_cast<float>(percent) / 100.0f;
    }

    /// Per-player gun button bindings: the evdev key code for each GunRole,
    /// pushed from the config each frame. Sized [2 players][kGunRoles].
    static constexpr usize kGunRoles = 8;
    void set_gun_buttons(const std::array<std::array<u32, kGunRoles>, 2>& b)
    {
        m_gun_buttons = b;
    }

    static constexpr usize kPadRoles = Config::kPadRoleCount;
    static constexpr usize kPadAxes  = Config::kPadAxisCount;

    /// The pad analogue role a game control reads, or PadAxisRole::kCount for
    /// none. `one_player` as for sample_channel().
    [[nodiscard]] static Config::PadAxisRole pad_axis_role(rom::AnalogControl control, bool one_player);

    /// The layout a game plays with: its own if it has one, else the default
    /// with that game's logical changes (`inputs` is its rom::InputFlags).
    [[nodiscard]] static Config::PadLayout game_layout(const Config& config, const std::string& family,
                                                       u32 inputs);

    /// The running game's gamepad layout, pushed from the config each frame.
    void set_pad_layout(const Config::PadLayout& layout);

    /// Whether the physical SDL `button` is down on `player`'s pad.
    [[nodiscard]] bool pad_physical_down(u32 player, s32 button) const;

    /// The lowest-numbered button held on `player`'s pad, or -1.
    [[nodiscard]] s32 pressed_pad_button(u32 player) const;

    /// A binding's name for the settings UI; empty if unbound or unknown.
    [[nodiscard]] static std::string pad_button_name(s32 button);

    /// The axis on `player`'s pad that has moved furthest from `baseline`, or
    /// -1; `positive` is its direction.
    [[nodiscard]] s32 captured_pad_axis(u32 player, const s16* baseline,
                                        bool* positive) const;

    /// Every axis on `player`'s pad, SDL_GAMEPAD_AXIS_COUNT of them, into `out`.
    void pad_axis_baseline(u32 player, s16* out) const;

    /// The raw reading of `player`'s steering/aim stick axis, with its invert
    /// applied, for the deadzone display. 0 without a pad.
    [[nodiscard]] s16 pad_steer_raw(u32 player) const;

    /// The name of an SDL gamepad axis, for the settings UI. Empty if unknown.
    [[nodiscard]] static std::string pad_axis_name(s32 axis);

    /// Present-stage placement, pushed from the config each frame, so the gun /
    /// mouse pointer maps through the same frame the backend draws the image
    /// into (render::compute_frame_map). A mismatch would offset every shot
    /// from the cursor.
    void set_present_placement(const render::PresentOptions& present) { m_present = present; }

    /// Widescreen columns each side of the 4:3 screen, 0 for none: a gun can
    /// then aim beside the screen, past its edge.
    void set_widescreen(u32 columns) { m_wide_columns = columns; }

    /// Whether the Sinden border is drawn. A gun aiming against that border
    /// reports positions on the game image; any other gun reports positions on
    /// the whole screen.
    void set_sinden_border(bool on) { m_sinden_border = on; }

    /// Bits to pull low on each port at a given frame, for unattended testing.
    struct ScriptedPress {
        u8 in0 = 0;  ///< Coins, start, service, test.
        u8 in1 = 0;  ///< Player 1.
    };

    /// Play a fixed sequence: two coins, start, then confirm a character, so an
    /// unattended capture gets past attract mode and the selection screens.
    /// `start1_bit` is the game's IN0 bit for IPT_START1 (`rom::GameSpec::start1_bit`).
    [[nodiscard]] static ScriptedPress scripted_press(u32 frame, u32 coin_frame,
                                                       u8 start1_bit = kStart1);

    /// Describe the bindings, for the help text.
    static void print_bindings();

private:
    /// One open gamepad and the player it drives.
    struct Pad {
        SDL_Gamepad*   handle = nullptr;
        SDL_JoystickID id     = 0;
        u32            player = 0;
        /// Last magnitudes sent, and frames since, so a lapsing effect is re-armed.
        u16            rumble_low  = 0;
        u16            rumble_high = 0;
        int            rumble_age  = 0;
        /// Shortcut controls down last frame, and those claimed by a shortcut.
        u8             shortcut_down    = 0;
        u8             shortcut_claimed = 0;
    };

    /// Whether the game sees this pad: not while it is in a shortcut.
    [[nodiscard]] static bool plays(const Pad& pad)
    {
        return pad.handle != nullptr && pad.shortcut_claimed == 0;
    }

    /// A steering wheel, opened through the joystick API because a wheel has no
    /// gamepad mapping and its steering axis wants the full 16 bits a gamepad
    /// stick throws away. Drives player one.
    struct Wheel {
        SDL_Joystick*  handle  = nullptr;
        SDL_JoystickID id      = 0;
        /// Force feedback, or null if the wheel has none. The constant force is
        /// set each frame from the game's drive-board command (drivers ignore
        /// FF_SPRING); the sine effect, where supported, carries the rumble.
        std::unique_ptr<WheelForce> ffb;
        int            force_level  = 0;   ///< Last level commanded, to skip no-ops.
        int            rumble_mag   = -1;  ///< last rumble magnitude, to skip no-ops.
        u16            rumble_period = 0;  ///< ms, of the last rumble sent; 0 before any.

        bool           autocenter = false;  ///< device autocentre still holding it.
        bool           can_rumble = false;  ///< has rumble motors of its own.

        /// Fallback when the device has no haptic effects: SDL's plain rumble,
        /// which goes straight to evdev FF_RUMBLE. Re-armed like the pad path,
        /// since a rumble lapses.
        u16            rumble_low  = 0;
        u16            rumble_high = 0;
        int            rumble_age  = 0;

        /// How many consecutive frames a one-directional constant force has been
        /// held, to decay a sustained crash push so a free-spinning PC wheel does
        /// not whip to the stop the way the cabinet's heavy wheel never could.
        int            constant_hold  = 0;
        int            constant_dir   = 0;  ///< sign of the held constant force.
        int            board_dir      = 0;  ///< the drive board's last push direction, +1 left.
        int            last_deflection = 0; ///< steering position last frame, for friction.

        /// Axis numbers on the device. Steering is the self-centring one;
        /// the pedals rest at one end. -1 means the device lacks it.
        int  steer_axis   = -1;
        int  accel_axis   = -1;
        int  brake_axis   = -1;
        bool accel_invert = false;
        bool brake_invert = false;
        bool accel_half   = false;  ///< pedal on half its axis, see WheelSettings.
        bool brake_half   = false;

        /// SDL reports 0 for an axis that has not sent an event yet, which on a
        /// pedal is half pressed. Until one moves, use its value read at open.
        static constexpr int      kMaxAxes = 8;
        std::array<s16, kMaxAxes> axis_rest{};
        u32                       axes_moved = 0;  ///< bitmask of axes seen moving.
    };

    [[nodiscard]] s16 wheel_axis(int axis) const;

    /// A joystick read for its axes beside the wheel: a pedal on another
    /// device, or one a calibration watches. Its axes rest as read at open
    /// until they move, as the wheel's.
    struct AxisSource {
        SDL_Joystick*                     handle = nullptr;
        SDL_JoystickID                    id     = 0;
        std::array<s16, Wheel::kMaxAxes>  rest{};
        u32                               moved  = 0;
        std::array<s16, Wheel::kMaxAxes>  baseline{};  ///< for a calibration.
    };
    [[nodiscard]] static bool open_source(SDL_JoystickID id, AxisSource* source);
    static void close_source(AxisSource* source);
    [[nodiscard]] static s16 source_axis(const AxisSource& source, int axis);

    /// Each pedal's own joystick, accel then brake; no handle: the wheel's.
    std::array<AxisSource, 2> m_pedal_sources{};
    /// A pedal the settings put on a device that is not connected: not read.
    std::array<bool, 2> m_pedal_lost{};
    /// (Re)opens the pedals' joysticks the settings name.
    void open_pedal_sources();

    /// The joysticks a calibration watches; the first is the wheel's.
    std::vector<AxisSource> m_capture_sources;
    bool                    m_capture_any = false;

    /// A pedal's raw axis, 0 accel, 1 brake: on its own device, or the wheel.
    [[nodiscard]] s16 pedal_axis(int pedal, int axis) const;

    /// How far a pedal is pressed, from 0 to 1.
    [[nodiscard]] float pedal_travel(int pedal, int axis, bool invert, bool half) const;


    void add_gamepad(SDL_JoystickID id);
    void remove_gamepad(SDL_JoystickID id);

    /// Open `id` as a wheel if it looks like one and no wheel is open yet.
    void add_wheel(SDL_JoystickID id);
    /// Whether the settings name this joystick as the wheel.
    [[nodiscard]] bool chosen_wheel(SDL_JoystickID id) const;
    /// Closes the wheel and opens it again from the joysticks connected.
    void reopen_wheel();
    /// The wheel's force feedback (again), on the device the settings choose.
    void open_force_feedback();
    void remove_wheel(SDL_JoystickID id);

    /// Give the open wheel's axes their roles: calibrated, or auto-detected.
    void assign_wheel_axes();

    /// The bound keys' scancodes from their names, and the keys taken from
    /// the default key map.
    void resolve_keys();

    /// The keyboard as the default key map reads it: a key bound to a role
    /// leaves its default one. Fills `buffer`; nullptr without a keyboard.
    [[nodiscard]] const bool* default_keys(std::array<bool, SDL_SCANCODE_COUNT>* buffer,
                                           int* count) const;

    /// Whether a bound key is held; false for SDL_SCANCODE_UNKNOWN (none).
    [[nodiscard]] static bool key_down(SDL_Scancode key);


    /// Read one driving control straight off the wheel, or a sentinel byte when
    /// the wheel has no axis for it. Steering is centred, pedals rest low.
    [[nodiscard]] bool sample_wheel_channel(const rom::AnalogChannel& channel,
                                            u8* out) const;

    /// The pad driving a given player, or nullptr if that player has none.
    [[nodiscard]] SDL_Gamepad* pad_for(u32 player) const;

    /// Whether `binding` is held on `pad`: a button, or an axis past half travel.
    [[nodiscard]] static bool binding_held(SDL_Gamepad* pad, s32 binding);

    /// Whether the pad's `physical` button is held, read through the user's
    /// bindings: a button that is a role's default reads that role's binding.
    [[nodiscard]] bool pad_button_held(const Pad& pad, SDL_GamepadButton physical) const;

    /// Guide + L3 / R3 press Service / Test into `port0`. True while Guide is
    /// held, when Start and Back are left alone so a host's own Guide+Start
    /// does not also reach the game.
    [[nodiscard]] static bool operator_chord(const Pad& pad, u8* port0);
    /// Whether the button bound to `role` is held on `player`'s pad.
    [[nodiscard]] bool pad_role_held(u32 player, Config::PadRole role) const;
    [[nodiscard]] bool pad_pedal_pressed(SDL_Gamepad* pad, u32 player, Config::PadAxisRole role) const;

    /// Sample one logical control and scale it into an analogue channel's own
    /// calibrated range, with the stick deadzone and curve shaping a stick axis.
    /// `one_player` puts a second stick on player 1's right stick (the lever).
    [[nodiscard]] u8 sample_channel(const rom::AnalogChannel& channel, float curve,
                                    bool one_player = false) const;

    void gather_lightguns(hw::Inputs* inputs, const rom::GameSpec& game) const;

    /// The player for a new pad: the one asking for its path, else the lowest
    /// free position not kept for another plugged-in pad. kPlayers when none is left.
    [[nodiscard]] u32 player_for(SDL_JoystickID id) const;
    [[nodiscard]] bool player_taken(u32 player) const;

    std::vector<Pad> m_pads;
    std::array<std::string, 2> m_pad_devices;
    std::array<std::string, 2> m_gun_devices;
    Wheel            m_wheel;
    WheelSettings    m_wheel_settings;

    /// Sequential-shifter state for a wheel's paddles, seeded from the game's
    /// start_gear. Mutable because the const poll() must remember the gear.
    mutable u32         m_wheel_gear = 1;    ///< 0 = neutral, 1..4 = the gears.
    mutable std::string m_gear_game;         ///< game the gate was seeded for.
    mutable bool m_gear_up_held    = false;
    mutable bool m_gear_down_held  = false;
    /// Frames of neutral left in a sequential shift between two gears.
    mutable u32  m_shift_neutral_frames = 0;

    /// Gamepad shoulder-button shifter edge state, shared with the wheel gate.
    mutable bool m_pad_gear_up_held   = false;
    mutable bool m_pad_gear_down_held = false;
    bool         m_menu_held       = false;  ///< edge state for the Menu-bound wheel button.

    /// The keys bound beside the wheel's and the pads' buttons, per role, and
    /// their names as last resolved; the keys they take from the default map.
    std::array<SDL_Scancode, Config::kWheelRoleCount>          m_wheel_keys{};
    std::array<std::array<SDL_Scancode, kPadRoles>, 2>         m_pad_keys{};
    std::array<std::string, Config::kWheelRoleCount>           m_wheel_key_names{};
    std::array<std::array<std::string, kPadRoles>, 2>          m_pad_key_names{};
    std::array<bool, SDL_SCANCODE_COUNT>                       m_key_taken{};
    /// Keys held when a capture began, not taken for the binding.
    std::array<bool, SDL_SCANCODE_COUNT>                       m_key_capture_held{};
    /// The shortcuts' keys, by name and as keycodes (set_hotkeys()).
    std::array<std::string, Config::kHotkeyCount> m_hotkey_names{};
    std::array<SDL_Keycode, Config::kHotkeyCount> m_hotkeys = [] {
        std::array<SDL_Keycode, Config::kHotkeyCount> keys{};
        keys.fill(SDLK_UNKNOWN);
        return keys;
    }();
    bool m_hotkeys_set = false;

    /// One of the shortcuts' keys, by what it types on this layout.
    [[nodiscard]] bool emulator_key(SDL_Scancode key) const;

    /// Desert Tank's forward/reverse shift.
    mutable bool m_desert_shift      = false;  ///< latched forward(false)/reverse(true).
    mutable bool m_desert_shift_held = false;  ///< edge state of the shift button.

    /// Gun-cursor position per player, 0..1 in game-image space, for the
    /// keyboard/pad aiming fallback on gun titles (mouse/dedicated gun preferred).
    /// Nudged by the pad sticks and keyboard arrows; starts centred.
    mutable std::array<float, kPlayers> m_gun_cursor_x = {0.5f, 0.5f};
    mutable std::array<float, kPlayers> m_gun_cursor_y = {0.5f, 0.5f};

    /// The pad/keyboard cursor holds a player's aim until the mouse moves again.
    mutable std::array<bool, kPlayers> m_gun_cursor_owns = {false, false};
    mutable float                      m_gun_last_ptr_x  = -1.0f;
    mutable float                      m_gun_last_ptr_y  = -1.0f;

    /// Per-device light guns, when built and present; null means the single-mouse
    /// path is used. Mutable because the const poll() drains their event queues.
#ifdef SM2_HAVE_LIGHTGUNS
    mutable std::unique_ptr<LightGuns> m_guns;
#endif

    /// Latest per-player aim in game-image space, for the crosshair overlay.
    /// Written by gather_lightguns, read by the GUI. Mutable for the same reason.
    mutable std::array<GunAim, kPlayers> m_gun_aims{};

    /// Each player's last raw aim and when it last moved or fired, for GunAim::idle.
    /// A gun counts as idle until it is first used.
    mutable std::array<bool, kPlayers>  m_gun_sampled{};
    mutable std::array<bool, kPlayers>  m_gun_used{};
    mutable std::array<float, kPlayers> m_gun_prev_x{};
    mutable std::array<float, kPlayers> m_gun_prev_y{};
    mutable std::array<u64, kPlayers>   m_gun_last_use_ms{};

    /// Recoil settings and the previous trigger level per gun, so a pulse fires
    /// once on the press edge rather than every frame the trigger is held.
    /// Sized to LightGuns::kMaxGuns (which is forward-declared here) plus slack.
    static constexpr usize kMaxGuns = 8;
    bool                             m_recoil_enabled  = true;
    bool                             m_pad_aim_centred = false;
    u32                              m_recoil_strength = 60;

    /// The drive board's current force command, and Sega Rally's chop setting:
    /// 0 is off, n chops the torque with a period of 2^(n+2) board ticks.
    DriveCommand                     m_drive_command;
    int                              m_drive_pulse = 0;
    BoardForce                       m_board_force;

    /// Daytona's drive board, which works out its forces from the wheel: fed
    /// the game's bytes, it drives a force-feedback wheel.
    DaytonaBoard                     m_daytona;

    /// Indy 500's drive board (Touring Car, Over Rev, Super GT), the same way.
    IndyBoard                        m_indy;
    /// Virtua Racing's, likewise.
    VrBoard                          m_vr;

    /// The Sega Rally board's interrupts counted since start, simulated: its
    /// chopping flips on bits of this count.
    double                           m_drive_ticks = 0.0;

    AgentHooks* m_agent = nullptr;
    [[nodiscard]] bool virtual_wheel() const { return m_agent != nullptr && m_agent->virtual_wheel; }
    void agent_ffb(const char* effect, int value)
    {
        if (m_agent != nullptr && m_agent->ffb) {
            m_agent->ffb(effect, value);
        }
    }

    /// The burst currently playing, shared by every pad: one drive board, one car.
    int                              m_pad_rumble_level    = 0;
    mutable std::array<u8, 8>        m_logged_analog{};    ///< last values logged at debug
    mutable u8                       m_logged_in1          = 0xff;
    int                              m_pad_rumble_dir      = 0;  ///< last jolt direction
    int                              m_pad_rumble_hold     = 0;  ///< frames left in the burst
    bool                             m_pad_rumble_enabled  = true;
    u32                              m_pad_rumble_strength = 60;
    bool                             m_pad_rumble_cornering = true;
    mutable std::array<bool, kMaxGuns> m_trigger_was_down{};

    /// Gun role -> evdev key code, per player; Sinden defaults until the config
    /// is applied. [player][role].
    std::array<std::array<u32, kGunRoles>, 2> m_gun_buttons = {{
        {0x110u, 0x111u, 0x101u, 0x102u, 0x105u, 0x106u, 0x107u, 0x108u},
        {0x110u, 0x111u, 0x101u, 0x102u, 0x105u, 0x106u, 0x107u, 0x108u},
    }};

    /// Bindings per player and role, SDL's layout until the config arrives.
    std::array<std::array<s32, kPadRoles>, 2> m_pad_buttons =
        Config::PadLayout{}.buttons;
    float m_pad_stick_response = 1.0f;
    float m_pad_stick_dead     = 32767.0f * 0.05f;

    /// Axis overrides per player and role, with invert flags and pedal buttons.
    std::array<std::array<s32, kPadAxes>, 2>  m_pad_axes        = Config::PadLayout{}.axes;
    std::array<std::array<bool, kPadAxes>, 2> m_pad_axis_invert = Config::PadLayout{}.axis_invert;
    std::array<std::array<s32, kPadAxes>, 2>  m_pad_axis_buttons = Config::PadLayout{}.axis_buttons;
    enum GunRoleIdx { GrTrigger, GrReload, GrCoin, GrStart,
                      GrHatUp, GrHatDown, GrHatLeft, GrHatRight };

    /// Present placement in effect, for the gun/pointer letterbox mapping.
    render::PresentOptions m_present;
    u32           m_wide_columns   = 0;
    bool          m_sinden_border  = false;

    bool             m_started = false;
};

}  // namespace sm2::osd
