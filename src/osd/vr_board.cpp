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
#include "osd/vr_board.h"

#include "core/archive.h"

#include <algorithm>

namespace sm2::osd {
namespace {

constexpr int kMaxPower = 63;

/// Interrupts the motor stands, its clutch held, before it may push again.
constexpr int kStopIrqs = 100;

}  // namespace

void VrBoard::command(u8 value)
{
    switch (value) {
        case 0x01: case 0x02: case 0x03: case 0x04: case 0x08: case 0x09:
            m_on     = false;
            m_effect = 0x10;
            return;
        case 0x05: case 0x06: case 0x07: case 0x0a:
            m_on = true;
            return;
        default:
            break;
    }
    // An effect received with the forces off is lost.
    const int group = value >> 4;
    if (group >= 1 && group <= 6 && m_on) {
        m_effect = value;
    }
}

int VrBoard::base_at(int position) const
{
    // Eleven places: the centre, then five each side, 2 more a place out.
    int places = 0;
    if (position < 0x71) {
        places = position < 0x31 ? 5 : 5 - ((position - 0x21) >> 4);
    } else if (position > 0x8f) {
        places = position >= 0xd0 ? 5 : ((position - 0x90) >> 4) + 1;
    }
    return 4 + 4 * m_dip + 2 * places;
}

VrBoard::Zone VrBoard::zone_at(int position)
{
    // The centre sits two steps lower for a wheel coming down.
    const int  low  = position >= m_reference ? 0x79 : 0x77;
    const int  high = low + 0x0f;
    const Zone zone = position < low ? Zone::Below : position > high ? Zone::Above : Zone::Centre;
    if (zone != m_zone) {
        m_zone      = zone;
        m_reference = position;
    }
    return zone;
}

void VrBoard::drive(Way way, int power)
{
    if (m_way == way) {
        m_power = power;
    } else if (m_way != Way::None) {
        m_way = Way::None;  // stops first, the clutch at its power: a brake
    } else if (m_counter >= kStopIrqs) {
        m_way   = way;
        m_power = power;
    }
}

void VrBoard::interrupt(int position)
{
    m_counter = std::min(m_counter + 1, 0xff);

    const int  level = m_effect & 0x0f;
    const int  base  = base_at(position);
    const Zone zone  = zone_at(position);
    switch (m_effect >> 4) {
        case 2:  // clutch
            m_way   = Way::None;
            m_power = level + base;
            break;
        case 3:  // centring: changes way at once
            m_way   = zone == Zone::Below ? Way::Up : zone == Zone::Above ? Way::Down : Way::None;
            m_power = zone == Zone::Centre ? 0 : level + base;
            break;
        case 4:  // uncentring
            if (zone == Zone::Centre) {
                m_way   = Way::None;
                m_power = 4 + 4 * m_dip - 1;
            } else {
                drive(zone == Zone::Below ? Way::Down : Way::Up, level + base + 1);
            }
            break;
        case 5:
            drive(Way::Down, level + base + 1);
            break;
        case 6:
            drive(Way::Up, level + base + 1);
            break;
        default:  // spring: the panel's alone
            m_way   = Way::None;
            m_power = 0;
            break;
    }
    if (m_way != Way::None) {
        m_counter = 0;
    }
}

VrBoard::Output VrBoard::step(int position, double irqs)
{
    m_irqs += irqs;
    while (m_irqs >= 1.0) {
        m_irqs -= 1.0;
        interrupt(position);
    }
    return Output{m_way, std::min(m_power, kMaxPower)};
}

void VrBoard::serialize(Archive& ar)
{
    ar.raw(m_on);
    ar.raw(m_effect);
    u8 way = static_cast<u8>(m_way);
    ar.raw(way);
    ar.raw(m_power);
    ar.raw(m_counter);
    u8 zone = static_cast<u8>(m_zone);
    ar.raw(zone);
    ar.raw(m_reference);
    ar.raw(m_irqs);
    if (ar.loading()) {
        if (way > static_cast<u8>(Way::Up) || zone > static_cast<u8>(Zone::Above)) {
            ar.mark_failed();
            return;
        }
        m_way  = static_cast<Way>(way);
        m_zone = static_cast<Zone>(zone);
    }
}

}  // namespace sm2::osd
