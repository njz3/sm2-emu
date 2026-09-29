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
// What the sound board asks of a Yamaha YMF292-F SCSP, so that more than one
// emulation of the chip can sit behind it: the MAME-derived one (ScspMame, in
// scsp.h) and the one being brought over from Mednafen (see SCSP.md). The sound
// board owns the memory the chip addresses (ScspMemory, in scsp_dsp.h) and hands
// it to whichever core it builds.
#pragma once

#include "core/types.h"

#include <functional>

namespace sm2 {
class Archive;
}

namespace sm2::hw {

class ScspCore {
public:
    virtual ~ScspCore() = default;

    ScspCore()                           = default;
    ScspCore(const ScspCore&)            = delete;
    ScspCore& operator=(const ScspCore&) = delete;

    virtual void reset() = 0;

    /// Called when a timer or the MIDI FIFO wants the sound 68000's attention.
    /// `level` is the 68000 interrupt level the SCSP has been programmed to use.
    using IrqHandler = std::function<void(int level, bool assert)>;

    /// Called when the SCSP wants the *host* CPU board's attention, over the
    /// MCIEB/MCIPD pair. Wired to the CPU board's interrupt latch.
    using MainIrqHandler = std::function<void(bool assert)>;

    /// A byte the sound program sent out of the MIDI port, bound for the host.
    using MidiOutHandler = std::function<void(u8 value)>;

    virtual void set_irq_handler(IrqHandler handler)          = 0;
    virtual void set_main_irq_handler(MainIrqHandler handler) = 0;
    virtual void set_midi_out_handler(MidiOutHandler handler) = 0;

    // -- register access, from the sound 68000 ------------------------------
    // `offset` is a word index, as MAME's read/write take it.

    [[nodiscard]] virtual u16 read(u32 offset)             = 0;
    virtual void write(u32 offset, u16 data, u16 mem_mask) = 0;

    // -- audio --------------------------------------------------------------

    /// Produce `frames` interleaved stereo samples at sample_rate().
    ///
    /// This is also where time passes for the SCSP: the timers, the envelopes,
    /// the LFOs and the MIDI transmitter all advance one step per frame, so the
    /// caller has to keep calling it whether or not anything is listening.
    virtual void generate(s16* output, u32 frames) = 0;

    [[nodiscard]] virtual u32 sample_rate() const = 0;

    /// Per-slot output gain in 1/256 units (256 == unity), applied to the whole
    /// voice. All unity leaves the mixer bit-exact. The hook for the per-set gain.
    virtual void set_slot_gains(const u16 gains[32]) = 0;

    /// A byte arrived from the host's UART.
    virtual void midi_in(u8 value) = 0;

    // -- inspection, for the headless bring-up test --------------------------

    struct Stats {
        u64 samples          = 0;
        u64 slot_starts      = 0;
        u64 timer_interrupts = 0;
        u64 midi_in_bytes    = 0;
        u64 midi_out_bytes   = 0;
        u64 dma_transfers    = 0;
        /// Largest absolute value either output channel has reached, so silence
        /// can be told from clipping.
        s32 peak_output      = 0;
    };
    [[nodiscard]] virtual const Stats& stats() const = 0;

    /// How many of the 32 slots are currently sounding. The sound board's bus
    /// contention is scaled by it.
    [[nodiscard]] virtual u32 active_slots() const = 0;

    /// Save/restore the whole chip. The memory and the callbacks are not part of
    /// it: the sound board wires those again.
    virtual void serialize(Archive& ar) = 0;
};

}  // namespace sm2::hw
