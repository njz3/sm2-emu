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
// The EPR-15111 drive board (Virtua Racing, motor cabinets): its behaviour. The
// motor turns one way or the other through a powder clutch; with no way set,
// the clutch brakes the wheel in proportion to the power.
#pragma once

#include "core/types.h"

namespace sm2 {
class Archive;
}

namespace sm2::osd {

class VrBoard {
public:
    /// The interrupt rate. No timer sets it and no board has been measured:
    /// 1 kHz is an estimate. Only the stop before a reversal depends on it.
    static constexpr double kIrqHz = 1000.0;

    /// Down lowers the wheel position, up raises it.
    enum class Way : u8 { None, Down, Up };

    struct Output {
        Way way   = Way::None;
        int power = 0;  ///< 0..63, the clutch's.
    };

    /// As at power-up: forces off, the effect 10.
    void reset()
    {
        const int dip = m_dip;
        *this         = VrBoard{};
        m_dip         = dip;
    }

    /// The operator's weight, switches 1-3 of the side's bank ON counting 0 to
    /// 7. The powers are those of a bank with switch 4 ON, which skips the
    /// start-up calibration: 4 more a step at every place of the wheel.
    void set_dip(int switches) { m_dip = switches; }

    /// A byte the game wrote to the board.
    void command(u8 value);

    /// Advances one frame, with the wheel at `position` (0..255), over `irqs`
    /// interrupts.
    [[nodiscard]] Output step(int position, double irqs);

    void serialize(Archive& ar);

    [[nodiscard]] bool forces_on() const { return m_on; }
    [[nodiscard]] u8   effect() const { return m_effect; }

private:
    enum class Zone : u8 { Below, Centre, Above };

    void interrupt(int position);
    void drive(Way way, int power);
    [[nodiscard]] int  base_at(int position) const;
    [[nodiscard]] Zone zone_at(int position);

    bool m_on     = false;
    u8   m_effect = 0x10;  ///< 1n-6n; 10 while the forces are off.
    int  m_dip    = 0;

    Way  m_way     = Way::None;
    int  m_power   = 0;
    int  m_counter = 0xff;  ///< Interrupts with no way set, saturated at FF.

    Zone   m_zone      = Zone::Centre;
    int    m_reference = 0x80;  ///< Where the wheel last changed zone.
    double m_irqs      = 0.0;
};

}  // namespace sm2::osd
