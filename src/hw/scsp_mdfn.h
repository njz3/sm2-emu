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
// The second SCSP core, scsp_core = mednafen (see SCSP.md). It follows the
// chip as Mednafen's hardware tests describe it: interrupts encoded from
// SCILV, free-running timers aligned to the sample counter, 4-byte MIDI FIFOs
// with their flags, DMA that leaves its registers alone. Written for sm2-emu;
// Mednafen served as a description of the hardware, not as code to translate.
//
// Where the port stands (SCSP.md, step 9): the control side -- register file,
// DMA, the three timers, the interrupt controller, the MIDI port and the slot
// monitor -- is in, so a sound program runs on it. Each slot reads its waveform (pitch,
// interpolation, the four loop modes, 8- and 16-bit PCM, noise, SBCTL), moved by
// its FM input from the sound stack, and runs its envelope and its LFO; the
// pitch LFO bends the pitch, and the envelope, TL and the amplitude LFO add up
// as one attenuation. Each slot's output goes back into the sound stack four
// slot periods late, and into the effects DSP's mix inputs. The DSP runs all
// 128 steps of its program every sample, its ring buffer accesses one step
// behind the instruction that asks for them, and its EFREG outputs come back
// through the slots' effect sends. The sends, MVOL and the output are the
// chip's integer arithmetic.
#pragma once

#include "core/types.h"
#include "hw/scsp_core.h"
#include "hw/scsp_dsp.h"

#include <array>
#include <utility>

namespace sm2::hw {

class ScspMednafen final : public ScspCore {
public:
    /// The sample rate is the clock over this, which is 44100 Hz exactly.
    static constexpr u32 kClockDivider = 512;

    ScspMednafen(ScspMemory& memory, u32 clock);

    void reset() override;

    void set_irq_handler(IrqHandler handler) override { m_irq_cb = std::move(handler); }
    void set_main_irq_handler(MainIrqHandler handler) override { m_main_irq_cb = std::move(handler); }
    void set_midi_out_handler(MidiOutHandler handler) override { m_midi_out_cb = std::move(handler); }

    [[nodiscard]] u16 read(u32 offset, u16 mem_mask) override;
    void write(u32 offset, u16 data, u16 mem_mask) override;

    void generate(s16* output, u32 frames) override;

    [[nodiscard]] u32 sample_rate() const override { return m_clock / kClockDivider; }

    void set_slot_gains(const u16 gains[32]) override;

    void midi_in(u8 value) override;

    [[nodiscard]] const Stats& stats() const override { return m_stats; }

    /// Slots still reading their waveform: from key-on until a one-shot ends
    /// or the envelope has all but died away. What the bus contention wants.
    [[nodiscard]] u32 active_slots() const override;

    void serialize(Archive& ar) override;

private:
    /// A 4-byte MIDI FIFO, as each direction has.
    struct MidiFifo {
        std::array<u8, 4> bytes{};
        u8                head  = 0;  ///< Next byte out.
        u8                count = 0;

        [[nodiscard]] bool empty() const { return count == 0; }
        [[nodiscard]] bool full() const { return count == bytes.size(); }
        void push(u8 value);
        /// The byte at the head, taken out when there is one. Reading an empty
        /// FIFO gives whatever the head slot last held.
        u8 pop();
    };

    struct Timer {
        u8  prescale = 0;   ///< Ticks every 2^prescale samples.
        u8  count    = 0;   ///< Raises its interrupt on reaching 0xff.
        s16 reload   = -1;  ///< Written value, loaded at the next tick; -1 for none.
    };

    /// Envelope phases, numbered as SGC reads them back.
    enum class Phase : u8 { Attack = 0, Decay1 = 1, Decay2 = 2, Release = 3 };

    /// A slot's state: where it is in its waveform, and its envelope. Positions
    /// count samples from SA and wrap at 16 bits, as the chip's address counter
    /// does. The envelope is an attenuation, 0 loudest to 0x3ff silent.
    struct Voice {
        u16   position = 0;      ///< Whole samples from SA.
        u16   fraction = 0;      ///< Between position and position + 1, 14 bits.
        bool  backward = false;  ///< Heading for LSA: reverse and alternating loops.
        bool  in_loop  = false;  ///< Has reached LSA once.
        bool  reading  = false;  ///< Fetching samples; a one-shot stops at LEA.
        Phase phase    = Phase::Release;
        u16   level    = 0x3ff;
    };

    /// A slot's LFO: one counter behind both the pitch and the amplitude LFO,
    /// free running whatever the slot is doing; a key-on leaves it alone.
    struct Lfo {
        u8  counter = 0;  ///< Where the LFO is in its 256-step cycle.
        u16 wait    = 1;  ///< Samples until the counter steps again.
    };

    /// What the effects DSP keeps from one step, and one sample, to the next:
    /// its latches, where its ring buffer stands, and the memory access that an
    /// instruction asked for and the next one carries out.
    struct Dsp {
        u32  inputs         = 0;  ///< INPUTS, 24 bits: what IRA last selected.
        u32  accumulator    = 0;  ///< SFT_REG, 26 bits: the last product plus B.
        u16  frc            = 0;  ///< FRC_REG, 13 bits.
        u32  y              = 0;  ///< Y_REG, 24 bits: INPUTS as YRL latched it.
        u16  adrs           = 0;  ///< ADRS_REG, 12 bits.
        u16  ring_offset    = 0;  ///< MDEC_CT: down by one every sample.
        u32  access_address = 0;  ///< Word address of the access in flight.
        u8   read_pending   = 0;  ///< 0 none, 1 a float read, 2 an integer one (NOFL).
        bool write_pending  = false;
        u16  write_value    = 0;
        u32  read_value     = 0;  ///< 24 bits, the last read, for IWT.
    };

    // -- register file, by byte address within the chip's 4 KB --------------

    [[nodiscard]] u16 register_read(u32 address, u16 mask);
    void              register_write(u32 address, u16 data, u16 mask);

    [[nodiscard]] u16 control_read(u32 address, u16 mask);
    void              control_write(u32 address, u16 data, u16 mask);

    // -- the parts of the chip that act ------------------------------------

    void run_dma();

    /// KRS applied to the slot's octave: added to every envelope rate.
    [[nodiscard]] unsigned key_scale(usize slot) const;

    /// One sample of the slot's envelope.
    void run_envelope(usize slot, unsigned scale);

    /// A pending KYONEX, for this slot: key on a released slot whose KYONB is
    /// set, release a sounding one whose KYONB is clear.
    void apply_key(usize slot, unsigned scale);

    /// What the envelope contributes to the slot's attenuation: its level, or 0
    /// while an attack is held or the envelope is bypassed.
    [[nodiscard]] unsigned envelope_output(usize slot) const;

    /// Latch what the monitor register shows of this slot (MSLC's).
    void monitor_slot(usize slot);

    /// Where the slot stands in the chip's cycle of 64 slot periods (two
    /// samples), which is how the sound stack is indexed.
    [[nodiscard]] unsigned slot_time(usize slot) const;

    /// The slot's FM input: the two sound-stack entries MDXSL and MDYSL name,
    /// added and scaled by MDL, in 64ths of a sample. 0 for MDL 4 and below.
    [[nodiscard]] s32 fm_input(usize slot) const;

    /// The slot's waveform sample for this sample period, before any level is
    /// applied, and its position moved on. Its loop has been followed already.
    [[nodiscard]] s16 play_slot(usize slot);

    /// Keep a voice's position inside what its loop mode allows, whether or not
    /// it is reading. Turning round mirrors the position, fraction included.
    static void follow_loop(Voice& voice, unsigned mode, u16 loop_start, u16 loop_end);

    /// One sample of the slot's LFO counter.
    void run_lfo(usize slot);

    /// The pitch LFO, added to the pitch mantissa (0x400 | FNS).
    [[nodiscard]] s32 pitch_lfo(usize slot) const;

    /// The amplitude LFO's share of the slot's attenuation, 0 to 254.
    [[nodiscard]] unsigned amplitude_lfo(usize slot) const;

    /// The slot's output: its sample under one attenuation from the envelope,
    /// TL and the amplitude LFO, or untouched with SDIR. What the sound stack
    /// takes and the sends carry.
    [[nodiscard]] s16 level_slot(usize slot, s16 sample) const;

    /// Add a slot's output to the direct output, through the per-set gain. The
    /// slot's effect return (EFREG, or EXTS) is added separately, in generate().
    void mix_slot(usize slot, s16 output, s32& left, s32& right) const;

    /// Send a slot's output to the DSP mix input ISEL at level IMXL, through
    /// the per-set gain like the direct output.
    void send_to_dsp(usize slot, s16 output);

    /// One sample of the effects DSP: the whole program over last sample's mix
    /// inputs, then the ring buffer moves on.
    void run_dsp();
    void tick_timers();
    void tick_midi_out();
    [[nodiscard]] u8 midi_flags() const;
    [[nodiscard]] u8 midi_read();
    void             midi_write(u8 value);

    /// Set interrupt sources pending, towards the sound CPU and the host alike.
    void raise(u16 sources);
    /// Recompute both interrupt outputs and tell the callbacks what changed.
    void update_interrupts();

    // -- wiring --------------------------------------------------------------

    ScspMemory* m_memory = nullptr;
    u32         m_clock  = 0;

    IrqHandler     m_irq_cb;
    MainIrqHandler m_main_irq_cb;
    MidiOutHandler m_midi_out_cb;

    // -- slots ---------------------------------------------------------------

    /// Each slot's 16 register words, as they read back.
    std::array<std::array<u16, 16>, 32> m_slot_regs{};
    std::array<Voice, 32>               m_voices{};
    std::array<Lfo, 32>                 m_lfos{};
    /// KYONEX was written: apply every slot's KYONB at the next sample.
    bool m_key_execute = false;

    /// The noise source: a 17-bit LFSR, clocked once per slot period whether
    /// or not a slot plays noise. Also the LFOs' noise waveform.
    u32 m_noise = 1;

    // -- common control ------------------------------------------------------

    u8   m_mvol         = 0;
    bool m_dac18        = false;
    bool m_mem4mb       = false;
    u8   m_rbp          = 0;  ///< DSP ring buffer base, in 4K-word units.
    u8   m_rbl          = 0;  ///< DSP ring buffer length, 8K words << m_rbl.
    u8   m_monitor_slot = 0;  ///< MSLC
    u16  m_monitor_data = 0;  ///< CA, SGC and EG of slot MSLC, latched as it runs

    MidiFifo m_midi_in;
    MidiFifo m_midi_out;
    bool     m_midi_in_overflow = false;
    u8       m_midi_tx_byte     = 0;
    u8       m_midi_tx_bits     = 0;  ///< Bits of m_midi_tx_byte still to go out.
    u32      m_midi_bit_phase   = 0;  ///< Bit clock: 32 bits every 45 samples.

    u32  m_dma_memory   = 0;  ///< DMEA, a word address (19 bits).
    u16  m_dma_register = 0;  ///< DRGA, a word address within the chip (11 bits).
    u16  m_dma_length   = 0;  ///< DTLG, in words (11 bits).
    bool m_dma_to_memory = false;  ///< DDIR: registers to memory when set.
    bool m_dma_gate      = false;  ///< DGATE: transfer zeroes.
    bool m_dma_running   = false;  ///< Not state: stops a DMA restarting itself.

    std::array<Timer, 3> m_timers{};

    u16                 m_scieb = 0;
    u16                 m_scipd = 0;
    u16                 m_mcieb = 0;
    u16                 m_mcipd = 0;
    std::array<u8, 3>   m_scilv{};
    int                 m_irq_level = 0;      ///< Last level given to the sound CPU.
    bool                m_main_irq  = false;  ///< Last state given to the host.

    // -- sound stack and effects DSP -----------------------------------------

    std::array<u16, 64>  m_sound_stack{};  ///< SOUS: the last 64 slot outputs, the FM inputs
    std::array<u16, 4>   m_stack_delay{};  ///< Slot outputs on their way into the sound stack
    std::array<u16, 64>  m_coef{};         ///< 13 bits each
    std::array<u16, 32>  m_madrs{};
    std::array<u64, 128> m_mpro{};
    std::array<u32, 128> m_temp{};   ///< 24 bits each, indexed from the ring offset by the DSP
    std::array<u32, 32>  m_mems{};   ///< 24 bits each
    std::array<u32, 16>  m_mixs{};   ///< 20 bits each, summed by the slots, cleared every sample
    std::array<u16, 16>  m_efreg{};
    std::array<u16, 2>   m_exts{};   ///< Unconnected on Model 2: always zero.
    Dsp                  m_dsp;

    // -- time and bookkeeping ------------------------------------------------

    /// Samples since reset. The timer prescalers divide this very counter.
    u64 m_sample_count = 0;

    std::array<u16, 32> m_slot_gain{};  ///< Per-set balance, 1/256 units.
    Stats               m_stats;
    bool                m_warned_test = false;
};

}  // namespace sm2::hw
