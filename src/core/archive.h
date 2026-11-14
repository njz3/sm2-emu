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

#include "core/types.h"

#include <deque>
#include <string>
#include <type_traits>
#include <vector>

// Binary save-state archive. One `serialize(Archive&)` per class both reads and
// writes, so the two directions cannot drift out of sync. The direction is fixed
// at construction.
//
// The file is host-endian and not portable across endianness; every target is
// little-endian and a static_assert enforces it.

namespace sm2 {

class Archive {
public:
    enum class Mode { Save, Load };

    /// Save mode: writes go into `buffer`, growing it.
    explicit Archive(std::vector<u8>& buffer);

    /// Load mode: reads come from `data`/`size`, bounds-checked. A read past
    /// the end sets `failed()` and yields zeroes rather than reading OOB.
    Archive(const u8* data, usize size);

    [[nodiscard]] bool saving() const { return m_mode == Mode::Save; }
    [[nodiscard]] bool loading() const { return m_mode == Mode::Load; }

    /// A trivially-copyable scalar or aggregate: raw little-endian bytes.
    template <class T>
    void raw(T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>,
                      "Archive::raw requires a trivially-copyable type");
        bytes(&value, 1);
    }

    /// A contiguous block of `count` trivially-copyable elements
    /// (std::array data, a fixed C array, a std::vector's data pointer).
    template <class T>
    void bytes(T* data, usize count)
    {
        static_assert(std::is_trivially_copyable_v<T>,
                      "Archive::bytes requires a trivially-copyable element");
        transfer(data, count * sizeof(T));
    }

    /// A length-prefixed byte vector. On load the vector is resized to the
    /// stored length before its contents are read.
    void vector_pod(std::vector<u8>& v);

    /// A length-prefixed std::deque<u32>: a u32 count followed by the elements.
    void deque_u32(std::deque<u32>& d);

    /// Set on Load when a read runs past the end of the input. Sticky: later
    /// reads yield zeroes, so it can be checked once after the whole payload.
    [[nodiscard]] bool failed() const { return m_failed; }

    /// Fails the load (e.g. on an out-of-range value) so the state is rolled back.
    void mark_failed() { m_failed = true; }

    /// The save-state format version of what is being read, so a serialize()
    /// can still read what an older version wrote. Saving always writes the
    /// current version (state::kFormatVersion), which is also the default.
    [[nodiscard]] u32 format_version() const;
    void set_format_version(u32 version) { m_format_version = version; }

    /// Bytes written so far (Save) or consumed so far (Load).
    [[nodiscard]] usize position() const { return m_pos; }

private:
    void transfer(void* data, usize num_bytes);

    Mode m_mode;

    // Save: the growable output. Load: leave null.
    std::vector<u8>* m_out = nullptr;

    // Load: the input span. Save: leave null.
    const u8* m_in      = nullptr;
    usize     m_in_size = 0;

    usize m_pos    = 0;
    bool  m_failed = false;

    u32 m_format_version = 0;  ///< 0 means the current version.
};

// ---------------------------------------------------------------------------
// File format
// ---------------------------------------------------------------------------
// A save-state file is a small header followed by the machine's serialize()
// payload:
//
//   magic     8 bytes  "KEISHOST" ("SM2STATE" in files from sm2-emu)
//   version   u32      bumped on any serialize-layout change
//   game_len  u32
//   game      game_len bytes  (GameSpec::name)
//   board     u32      (rom::Board, cast by the caller so sm2_core does not
//                       depend on sm2_rom)
//   payload   the machine's serialize() output
//
// The header is validated before the payload, so a wrong magic, version, game
// or board is rejected without touching the running machine.
//
// Versions (this fork's numbering; upstream's own 2 and 3 are not these):
//   1  the first format
//   2  the sound board records which SCSP core wrote its state (SCSP.md).
//      Version 1 files still load: they can only come from the MAME-derived
//      core, and Model2Sound reads them as such.
//   3  the MAME-derived SCSP core saves its output filter's state (upstream's
//      PR #9). Versions 1 and 2 still load, the filter as reset.
//   4  the geometrizer, on every board, saves the state its display list sets
//      only once (upstream's v0.9.45, its own format 2). Versions 1 to 3 still
//      load, that state as it stands.
//   5  Model 2B and 2C save the EPR-18261 drive board's answer on port D (its
//      last command and the centre the game set). Versions 1 to 4 still load,
//      that answer as it stands.
//   6  the sound board saves its 68000 bus contention carry. Older versions
//      load it as zero.
//   7  the link boards save the frames still in flight (upstream's v0.9.60,
//      its own format 3). Older versions load with none in flight.

namespace state {

inline constexpr char     kMagic[8]      = {'K', 'E', 'I', 'S', 'H', 'O', 'S', 'T'};
/// The magic of states saved by sm2-emu, still read.
inline constexpr char     kLegacyMagic[8] = {'S', 'M', '2', 'S', 'T', 'A', 'T', 'E'};
inline constexpr u32      kFormatVersion = 7;

/// The oldest version read_header() accepts. Every serialize() that changed
/// layout since has to keep reading this one (see Archive::format_version).
inline constexpr u32 kOldestReadableVersion = 1;

/// Header fields, in file order. The game name and board are supplied by the
/// caller so this stays free of the rom layer.
struct Header {
    u32         version = kFormatVersion;  ///< Filled by read_header; write_header ignores it.
    std::string game;
    u32         board   = 0;
};

/// Write the header into `ar` (Save mode); the caller's payload follows.
void write_header(Archive& ar, const Header& header);

/// Read and validate a header from a raw file buffer. Returns true and fills
/// `out` and `payload_offset` only when the magic and version match; otherwise
/// logs a reason. Matching game and board is the caller's job.
[[nodiscard]] bool read_header(const u8* data, usize size, Header& out,
                               usize& payload_offset);

}  // namespace state

inline u32 Archive::format_version() const
{
    return m_format_version != 0 ? m_format_version : state::kFormatVersion;
}

}  // namespace sm2
