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
// See scsp_mdfn.h.

#include "hw/scsp_mdfn.h"

#include "core/archive.h"
#include "core/log.h"

#include <algorithm>
#include <bit>
#include <utility>

namespace sm2::hw {

namespace {

// -- the chip's 4 KB register space, by byte address --------------------------

constexpr u32 kSlotsEnd      = 0x400;  ///< 32 slots of 0x20 bytes
constexpr u32 kControlBase   = 0x400;
constexpr u32 kControlEnd    = 0x430;
constexpr u32 kSoundStack    = 0x600;  ///< SOUS, 64 words
constexpr u32 kSoundStackEnd = 0x680;
constexpr u32 kCoef          = 0x700;  ///< 64 words, 13 bits in bits 15..3
constexpr u32 kCoefEnd       = 0x780;
constexpr u32 kMadrs         = 0x780;  ///< 32 words
constexpr u32 kMadrsEnd      = 0x7c0;
constexpr u32 kMpro          = 0x800;  ///< 128 steps of 64 bits
constexpr u32 kMproEnd       = 0xc00;
constexpr u32 kTemp          = 0xc00;  ///< 128 x 24 bits, two words each
constexpr u32 kTempEnd       = 0xe00;
constexpr u32 kMems          = 0xe00;  ///< 32 x 24 bits, two words each
constexpr u32 kMemsEnd       = 0xe80;
constexpr u32 kMixs          = 0xe80;  ///< 16 x 20 bits, two words each
constexpr u32 kMixsEnd       = 0xec0;
constexpr u32 kEfreg         = 0xec0;  ///< 16 words
constexpr u32 kEfregEnd      = 0xee0;
constexpr u32 kExts          = 0xee0;  ///< 2 words, read only
constexpr u32 kExtsEnd       = 0xee4;

// -- interrupt sources: the bits of SCIEB/SCIPD/SCIRE and MCIEB/MCIPD/MCIRE ----

constexpr u16 kIrqMidiIn   = 1u << 3;   ///< a byte waits in the input FIFO
constexpr u16 kIrqDma      = 1u << 4;   ///< a DMA has finished
constexpr u16 kIrqCpu      = 1u << 5;   ///< set by a program writing SCIPD/MCIPD
constexpr u16 kIrqTimerA   = 1u << 6;   ///< timers B and C are the next two bits
constexpr u16 kIrqMidiOut  = 1u << 9;   ///< the output FIFO has drained
constexpr u16 kIrqSample   = 1u << 10;  ///< once per sample
constexpr u16 kIrqSources  = 0x07ff;
constexpr unsigned kIrqSourceCount = 11;

// -- MIDI -----------------------------------------------------------------------

/// The flags in the high byte of register 0x404, above the input data.
constexpr u8 kMidiInEmpty    = 1u << 0;
constexpr u8 kMidiInFull     = 1u << 1;
constexpr u8 kMidiInOverflow = 1u << 2;
constexpr u8 kMidiOutEmpty   = 1u << 3;
constexpr u8 kMidiOutFull    = 1u << 4;

/// The MIDI bit clock is the chip's own clock over 720: 32 bits every 45
/// samples, 31,360 baud at 44.1 kHz, within MIDI's 1% of 31,250. A byte is eight
/// data bits framed by a start and a stop bit.
constexpr u32 kMidiBitsPerStep    = 32;
constexpr u32 kMidiSamplesPerStep = 45;
constexpr u8  kMidiBitsPerByte    = 10;

// -- slot register words ---------------------------------------------------

constexpr u32 kSlotRegKey       = 0;   ///< KYONEX, KYONB, SBCTL, SSCTL, LPCTL, PCM8B, SA 19..16
constexpr u32 kSlotRegStart     = 1;   ///< SA 15..0
constexpr u32 kSlotRegLoopStart = 2;   ///< LSA
constexpr u32 kSlotRegLoopEnd   = 3;   ///< LEA
constexpr u32 kSlotRegRates     = 4;   ///< D2R, D1R, EGHOLD, AR
constexpr u32 kSlotRegRelease   = 5;   ///< EG bypass, LPSLNK, KRS, DL, RR
constexpr u32 kSlotRegLevel     = 6;   ///< STWINH, SDIR, TL: 12 bits read back
constexpr u32 kSlotRegFm        = 7;   ///< MDL, MDXSL, MDYSL
constexpr u32 kSlotRegPitch     = 8;   ///< OCT, FNS
constexpr u32 kSlotRegLfo       = 9;   ///< LFORE, LFOF, PLFOWS, PLFOS, ALFOWS, ALFOS
constexpr u32 kSlotRegMix       = 10;  ///< ISEL, IMXL: 8 bits read back
constexpr u32 kSlotRegOutput    = 11;  ///< DISDL, DIPAN, EFSDL, EFPAN
constexpr u32 kSlotRegCount     = 12;  ///< words 12..15 read as zero

constexpr u16 kKeyExecute  = 0x1000;  ///< KYONEX, a strobe
constexpr u16 kKeyOn       = 0x0800;  ///< KYONB
constexpr u16 kPcm8        = 0x0010;  ///< PCM8B
constexpr u16 kAttackHold  = 0x0020;  ///< EGHOLD: full level while in attack
constexpr u16 kLoopLink    = 0x4000;  ///< LPSLNK: the attack ends on reaching LSA
constexpr u16 kEgBypass    = 0x8000;  ///< the envelope counts as 0 (TL and ALFO still apply)
constexpr u16 kSoundDirect = 0x0100;  ///< SDIR: no envelope, TL or ALFO at all
constexpr u16 kStackWriteInhibit = 0x0200;  ///< STWINH: the slot's output stays out of the sound stack
constexpr u16 kShortWave   = 0x8000;  ///< pitch word bit 15: wrap the wave at a power of two
constexpr u16 kLfoReset    = 0x8000;  ///< LFORE: holds the LFO counter at 0

/// LFO waveforms, PLFOWS and ALFOWS.
constexpr unsigned kLfoSaw      = 0;
constexpr unsigned kLfoSquare   = 1;
constexpr unsigned kLfoTriangle = 2;
constexpr unsigned kLfoNoise    = 3;

/// SSCTL, where a slot's samples come from.
constexpr unsigned kSourceMemory = 0;
constexpr unsigned kSourceNoise  = 1;  ///< 2 is silence, 3 is undefined and taken as 2

/// LPCTL.
constexpr unsigned kLoopOff       = 0;  ///< play to LEA, then stop reading
constexpr unsigned kLoopNormal    = 1;  ///< LSA to LEA, over and over
constexpr unsigned kLoopReverse   = 2;  ///< once to LSA, then LEA back to LSA, over and over
constexpr unsigned kLoopAlternate = 3;  ///< LSA to LEA and back, over and over

/// SBCTL: what every sample read is XORed with, before interpolation. It is how
/// the chip takes offset-binary or sign-inverted data.
constexpr std::array<u16, 4> kSbctlXor = {0x0000, 0x7fff, 0x8000, 0xffff};

/// A slot's position between two samples, in bits of fraction.
constexpr unsigned kPhaseBits = 14;
constexpr u32      kPhaseMask = (1u << kPhaseBits) - 1u;

// -- levels ------------------------------------------------------------------
//
// Levels are attenuations on a 10-bit scale, 6 dB for every 64 steps: 0 is full
// level, 0x3ff silence. The envelope, TL (four steps a unit) and the amplitude
// LFO add up on that scale before anything is multiplied.

constexpr u16 kSilent      = 0x3ff;
constexpr u16 kAttackStart = 0x280;  ///< where a key-on starts an attack that is not instant
constexpr u16 kReadCutoff  = 0x3c0;  ///< at this attenuation a slot stops reading memory

constexpr u32 kDefaultClock = 22'579'200;

[[nodiscard]] u16 merge(u16 old, u16 data, u16 mask)
{
    return static_cast<u16>((old & ~mask) | (data & mask));
}

/// The pitch mantissa, 1.0 being 0x400: FNS below the implicit 1. The chip
/// takes FNS as 11 bits, and bit 10, which the manual leaves unused, cancels
/// the implicit 1 rather than adding to it.
[[nodiscard]] u32 pitch_mantissa(u16 pitch)
{
    return 0x400u ^ (pitch & 0x7ffu);
}

/// The chip's FM offsets are 11-bit signed sample counts.
[[nodiscard]] s32 wrap11(s32 value)
{
    return ((value & 0x7ff) ^ 0x400) - 0x400;
}

/// The low `bits` bits of `value`, as a signed number.
[[nodiscard]] s32 sign_extend(u32 value, unsigned bits)
{
    const u32 sign = 1u << (bits - 1);
    return static_cast<s32>(((value & ((sign << 1) - 1)) ^ sign) - sign);
}

/// The DSP keeps its ring buffer in 16-bit floats: a sign, a 4-bit exponent
/// and 11 bits of mantissa. The exponent counts the bits below the sign that
/// repeat it, up to 11, and the first one that does not is implied; from 12 on
/// all 11 did, and the implied bit repeats the sign as well.
[[nodiscard]] u32 dsp_float_to_int(u16 value)
{
    const bool     negative = (value & 0x8000u) != 0;
    const unsigned exponent = (value >> 11) & 0xfu;
    const bool     implied  = (exponent < 12) != negative;
    // Sign, implied bit and mantissa: the top 13 bits of the 24.
    const u32 top = (negative ? 0x1000u : 0u) | (implied ? 0x800u : 0u) | (value & 0x7ffu);
    return static_cast<u32>((sign_extend(top, 13) * 2048) >> std::min(exponent, 11u)) & 0xffffffu;
}

[[nodiscard]] u16 dsp_int_to_float(u32 value)
{
    const bool negative = (value & 0x800000u) != 0;
    // The bits below the sign that repeat it, as leading zeros; 12 at most.
    const u32      below    = (negative ? ~value : value) & 0x7fffffu;
    const unsigned exponent = static_cast<unsigned>(std::min(12, std::countl_zero(below << 9)));
    const u32      mantissa = ((value << std::min(exponent, 11u)) >> 11) & 0x7ffu;
    return static_cast<u16>((negative ? 0x8000u : 0u) | (exponent << 11) | mantissa);
}

/// Apply an attenuation to a sample. The top four bits halve it, the low six
/// scale it from 127/128 down to 64/128, which is the next halving.
[[nodiscard]] s32 attenuate(s32 sample, unsigned level)
{
    return (sample * static_cast<s32>((level & 0x3fu) ^ 0x7fu)) >> ((level >> 6) + 7);
}

/// Whether an envelope at `rate` (1..31, key scaling included) moves on this
/// sample. From 24 up it moves every other sample. Below, the sample counter's
/// lowest set bit picks the samples: one in 2^(W+1) for an even rate, three in
/// 2^(W+3) for an odd one, W being (24 - rate) / 2.
[[nodiscard]] bool envelope_ticks(unsigned rate, u64 sample)
{
    if (rate >= 24) {
        return (sample & 1) == 0;
    }
    if (sample == 0 || (sample & 1) != 0) {
        return false;
    }
    const unsigned lowest = static_cast<unsigned>(std::countr_zero(sample));
    const unsigned wait   = (24u - rate) >> 1;
    return (rate & 1u) == 0 ? lowest == wait : (lowest == wait + 1 || lowest == wait + 2);
}

/// How far a tick moves the envelope, as a right shift: a decay or release adds
/// 16 >> shift, an attack takes (level + 1) >> shift off, rounded up. Rates
/// below 24 share the smallest step; the odd rates from 25 alternate between
/// the two steps either side.
[[nodiscard]] unsigned envelope_shift(unsigned rate, u64 sample)
{
    const unsigned clamped = std::clamp(rate, 24u, 30u);
    return ((32u - clamped) >> 1) + ((clamped & 1u) & static_cast<unsigned>(sample >> 1));
}

/// DISDL/EFSDL and DIPAN/EFPAN as the two gains of a send, 1.0 being 0x4000.
/// The level is 6 dB a step down from 7, 0 muting; the pan takes 3 dB a step
/// off one side -- an odd step as a quarter off the even step below it, 15
/// muting -- and bit 4 picks the side, the left one when clear.
struct SendGains {
    s32 left  = 0;
    s32 right = 0;
};

[[nodiscard]] SendGains send_gains(unsigned level, unsigned pan)
{
    const s32 full = level == 0 ? 0 : s32{0x80} << level;
    s32       side = full >> ((pan & 0xfu) >> 1);
    if ((pan & 1u) != 0) {
        side -= side >> 2;
    }
    if ((pan & 0xfu) == 0xfu) {
        side = 0;
    }
    return (pan & 0x10u) != 0 ? SendGains{full, side} : SendGains{side, full};
}

/// MVOL as a gain, 1.0 being 0x100: 3 dB a step down from 15, the even steps as
/// a quarter off the odd step above, 0 muting.
[[nodiscard]] s32 master_gain(unsigned mvol)
{
    if (mvol == 0) {
        return 0;
    }
    s32 gain = s32{2} << (mvol >> 1);
    if ((mvol & 1u) == 0) {
        gain -= gain >> 2;
    }
    return gain;
}

/// The DSP's wide words take two register words: the one at +0 holds the low
/// `low_bits` bits, the one at +2 the 16 above them.
[[nodiscard]] u16 wide_read(u32 value, bool upper, unsigned low_bits)
{
    return upper ? static_cast<u16>(value >> low_bits)
                 : static_cast<u16>(value & ((1u << low_bits) - 1));
}

void wide_write(u32& value, bool upper, unsigned low_bits, u16 data, u16 mask)
{
    const u32 low_mask = (1u << low_bits) - 1;
    if (upper) {
        const u16 part = merge(static_cast<u16>(value >> low_bits), data, mask);
        value          = (value & low_mask) | (u32{part} << low_bits);
    } else {
        const u16 part = merge(static_cast<u16>(value & low_mask), data, mask);
        value          = (value & ~low_mask) | (part & low_mask);
    }
    value &= (1u << (low_bits + 16)) - 1;
}

}  // namespace

// ---------------------------------------------------------------------------
// MIDI FIFO
// ---------------------------------------------------------------------------

void ScspMednafen::MidiFifo::push(u8 value)
{
    bytes[(head + count) % bytes.size()] = value;
    ++count;
}

u8 ScspMednafen::MidiFifo::pop()
{
    const u8 value = bytes[head];
    if (count != 0) {
        head = static_cast<u8>((head + 1) % bytes.size());
        --count;
    }
    return value;
}

// ---------------------------------------------------------------------------
// Construction and reset
// ---------------------------------------------------------------------------

ScspMednafen::ScspMednafen(ScspMemory& memory, u32 clock)
    : m_memory(&memory), m_clock(clock != 0 ? clock : kDefaultClock)
{
    m_slot_gain.fill(256);
    reset();
}

void ScspMednafen::reset()
{
    m_slot_regs   = {};
    m_voices      = {};
    m_lfos        = {};
    m_key_execute = false;
    m_noise       = 1;

    m_mvol         = 0;
    m_dac18        = false;
    m_mem4mb       = false;
    m_rbp          = 0;
    m_rbl          = 0;
    m_monitor_slot = 0;
    m_monitor_data = 0;

    m_midi_in          = {};
    m_midi_out         = {};
    m_midi_in_overflow = false;
    m_midi_tx_byte     = 0;
    m_midi_tx_bits     = 0;
    m_midi_bit_phase   = 0;

    m_dma_memory    = 0;
    m_dma_register  = 0;
    m_dma_length    = 0;
    m_dma_to_memory = false;
    m_dma_gate      = false;
    m_dma_running   = false;

    m_timers = {};

    m_scieb = 0;
    m_scipd = 0;
    m_mcieb = 0;
    m_mcipd = 0;
    m_scilv = {};

    m_sound_stack = {};
    m_stack_delay = {};
    m_coef        = {};
    m_madrs       = {};
    m_mpro        = {};
    m_temp        = {};
    m_mems        = {};
    m_mixs        = {};
    m_efreg       = {};
    m_exts        = {};
    m_dsp         = Dsp{};

    m_sample_count = 0;
    m_stats        = Stats{};

    // Nothing is pending any more, so let go of both interrupt outputs.
    m_irq_level = 0;
    m_main_irq  = false;
    if (m_irq_cb) {
        m_irq_cb(0, false);
    }
    if (m_main_irq_cb) {
        m_main_irq_cb(false);
    }
}

void ScspMednafen::set_slot_gains(const u16 gains[32])
{
    std::copy(gains, gains + 32, m_slot_gain.begin());
}

u32 ScspMednafen::active_slots() const
{
    return static_cast<u32>(std::count_if(m_voices.begin(), m_voices.end(),
                                          [](const Voice& voice) { return voice.reading; }));
}

// ---------------------------------------------------------------------------
// Register access
// ---------------------------------------------------------------------------

u16 ScspMednafen::read(u32 offset, u16 mem_mask)
{
    return register_read(offset * 2, mem_mask);
}

void ScspMednafen::write(u32 offset, u16 data, u16 mem_mask)
{
    register_write(offset * 2, data, mem_mask);
}

u16 ScspMednafen::register_read(u32 address, u16 mask)
{
    address &= 0xffe;

    if (address < kSlotsEnd) {
        return m_slot_regs[address >> 5][(address >> 1) & 0xf];
    }
    if (address >= kControlBase && address < kControlEnd) {
        return control_read(address, mask);
    }
    if (address >= kSoundStack && address < kSoundStackEnd) {
        return m_sound_stack[(address - kSoundStack) >> 1];
    }
    if (address >= kCoef && address < kCoefEnd) {
        return static_cast<u16>(m_coef[(address - kCoef) >> 1] << 3);
    }
    if (address >= kMadrs && address < kMadrsEnd) {
        return m_madrs[(address - kMadrs) >> 1];
    }
    if (address >= kMpro && address < kMproEnd) {
        // A step is four words, the most significant first.
        const unsigned shift = (3u - ((address >> 1) & 3u)) * 16u;
        return static_cast<u16>(m_mpro[(address - kMpro) >> 3] >> shift);
    }
    if (address >= kTemp && address < kTempEnd) {
        return wide_read(m_temp[(address - kTemp) >> 2], (address & 2) != 0, 8);
    }
    if (address >= kMems && address < kMemsEnd) {
        return wide_read(m_mems[(address - kMems) >> 2], (address & 2) != 0, 8);
    }
    if (address >= kMixs && address < kMixsEnd) {
        return wide_read(m_mixs[(address - kMixs) >> 2], (address & 2) != 0, 4);
    }
    if (address >= kEfreg && address < kEfregEnd) {
        return m_efreg[(address - kEfreg) >> 1];
    }
    if (address >= kExts && address < kExtsEnd) {
        return m_exts[(address - kExts) >> 1];
    }
    return 0;
}

void ScspMednafen::register_write(u32 address, u16 data, u16 mask)
{
    address &= 0xffe;

    if (address < kSlotsEnd) {
        const u32 index = (address >> 1) & 0xf;
        u16&      reg   = m_slot_regs[address >> 5][index];
        u16       value = merge(reg, data, mask);
        if (index == kSlotRegKey) {
            // KYONEX keys every slot at once, from each one's KYONB, and reads
            // back as 0.
            if ((value & kKeyExecute) != 0) {
                m_key_execute = true;
            }
            value &= static_cast<u16>(~kKeyExecute);
        } else if (index == kSlotRegLevel) {
            value &= 0x0fff;
        } else if (index == kSlotRegMix) {
            value &= 0x00ff;
        } else if (index >= kSlotRegCount) {
            value = 0;
        }
        reg = value;
        return;
    }
    if (address >= kControlBase && address < kControlEnd) {
        control_write(address, data, mask);
        return;
    }
    if (address >= kSoundStack && address < kSoundStackEnd) {
        u16& word = m_sound_stack[(address - kSoundStack) >> 1];
        word      = merge(word, data, mask);
        return;
    }
    if (address >= kCoef && address < kCoefEnd) {
        u16& coef = m_coef[(address - kCoef) >> 1];
        coef      = static_cast<u16>(merge(static_cast<u16>(coef << 3), data, mask) >> 3);
        return;
    }
    if (address >= kMadrs && address < kMadrsEnd) {
        u16& word = m_madrs[(address - kMadrs) >> 1];
        word      = merge(word, data, mask);
        return;
    }
    if (address >= kMpro && address < kMproEnd) {
        u64&           step  = m_mpro[(address - kMpro) >> 3];
        const unsigned shift = (3u - ((address >> 1) & 3u)) * 16u;
        const u16      part  = merge(static_cast<u16>(step >> shift), data, mask);
        step = (step & ~(u64{0xffff} << shift)) | (u64{part} << shift);
        return;
    }
    if (address >= kTemp && address < kTempEnd) {
        wide_write(m_temp[(address - kTemp) >> 2], (address & 2) != 0, 8, data, mask);
        return;
    }
    if (address >= kMems && address < kMemsEnd) {
        wide_write(m_mems[(address - kMems) >> 2], (address & 2) != 0, 8, data, mask);
        return;
    }
    if (address >= kMixs && address < kMixsEnd) {
        wide_write(m_mixs[(address - kMixs) >> 2], (address & 2) != 0, 4, data, mask);
        return;
    }
    if (address >= kEfreg && address < kEfregEnd) {
        u16& word = m_efreg[(address - kEfreg) >> 1];
        word      = merge(word, data, mask);
        return;
    }
    // EXTS is read only; 0x7c0..0x7ff and the gaps take nothing (Dead or
    // Alive clears 0x7c0.. after loading MADRS).
}

u16 ScspMednafen::control_read(u32 address, u16 mask)
{
    switch (address - kControlBase) {
        case 0x04: {
            // The MIDI flags, as they stand before this read, above the next
            // input byte. Only a read of the data lane takes that byte out.
            const u8 flags = midi_flags();
            const u8 data  = (mask & 0x00ff) != 0 ? midi_read() : u8{0};
            return static_cast<u16>((flags << 8) | data);
        }
        case 0x08:
            // The monitor of slot MSLC as it was when that slot last ran, not
            // MSLC itself (see monitor_slot()).
            return m_monitor_data;
        case 0x16:
            // DEXE, DDIR and DGATE read back; DEXE is always clear by then,
            // since a DMA completes as it is started.
            return static_cast<u16>((m_dma_to_memory ? 0x2000 : 0) | (m_dma_gate ? 0x4000 : 0));
        case 0x1e:
            return m_scieb;
        case 0x20:
            return m_scipd;
        case 0x2c:
            return m_mcipd;
        default:
            // Everything else in here is write only and reads as 0: MVOL, RBP,
            // MOBUF, the DMA addresses, the timers, SCILV, MCIEB and the resets.
            return 0;
    }
}

void ScspMednafen::control_write(u32 address, u16 data, u16 mask)
{
    switch (address - kControlBase) {
        case 0x00: {
            const u16 old = static_cast<u16>(m_mvol | (m_dac18 ? 0x100 : 0) | (m_mem4mb ? 0x200 : 0));
            const u16 value = merge(old, data, mask);
            m_mvol   = static_cast<u8>(value & 0xf);
            m_dac18  = (value & 0x100) != 0;
            m_mem4mb = (value & 0x200) != 0;
            break;
        }
        case 0x02: {
            const u16 value = merge(static_cast<u16>(m_rbp | (m_rbl << 7)), data, mask);
            m_rbp = static_cast<u8>(value & 0x7f);
            m_rbl = static_cast<u8>((value >> 7) & 3);
            break;
        }
        case 0x06:
            // MOBUF: the byte goes out only when its lane is written.
            if ((mask & 0x00ff) != 0) {
                midi_write(static_cast<u8>(data));
            }
            break;
        case 0x08: {
            const u16 value = merge(static_cast<u16>(m_monitor_slot << 11), data, mask);
            m_monitor_slot  = static_cast<u8>((value >> 11) & 0x1f);
            break;
        }
        case 0x10:
            // TEST: its bits upset the chip in ways no game relies on.
            if ((data & mask) != 0 && !m_warned_test) {
                m_warned_test = true;
                SM2_WARN("scsp (mednafen): TEST register written with %04x; ignored",
                         static_cast<unsigned>(data & mask));
            }
            break;
        case 0x12: {
            // DMEA bits 15..1 of the byte address, a word address's 14..0.
            const u16 value = merge(static_cast<u16>((m_dma_memory & 0x7fff) << 1), data, mask);
            m_dma_memory    = (m_dma_memory & ~0x7fffu) | ((value >> 1) & 0x7fffu);
            break;
        }
        case 0x14: {
            // DMEA's top four bits above DRGA.
            const u16 old   = static_cast<u16>((((m_dma_memory >> 15) & 0xf) << 12) | (m_dma_register << 1));
            const u16 value = merge(old, data, mask);
            m_dma_memory    = (m_dma_memory & 0x7fffu) | (u32{static_cast<u16>((value >> 12) & 0xf)} << 15);
            m_dma_register  = static_cast<u16>((value >> 1) & 0x7ff);
            break;
        }
        case 0x16: {
            const u16 old = static_cast<u16>((m_dma_length << 1) | (m_dma_to_memory ? 0x2000 : 0)
                                             | (m_dma_gate ? 0x4000 : 0));
            const u16 value = merge(old, data, mask);
            m_dma_length    = static_cast<u16>((value >> 1) & 0x7ff);
            m_dma_to_memory = (value & 0x2000) != 0;
            m_dma_gate      = (value & 0x4000) != 0;
            if ((data & mask & 0x1000) != 0) {
                run_dma();
            }
            break;
        }
        case 0x18:
        case 0x1a:
        case 0x1c: {
            // TxCTL above TIMx. The count is loaded at the timer's next tick, and
            // only when its lane was written.
            Timer&    timer = m_timers[(address - kControlBase - 0x18) >> 1];
            const u16 value = merge(static_cast<u16>(timer.prescale << 8), data, mask);
            timer.prescale  = static_cast<u8>((value >> 8) & 7);
            if ((mask & 0x00ff) != 0) {
                timer.reload = static_cast<s16>(data & 0xff);
            }
            break;
        }
        case 0x1e:
            m_scieb = static_cast<u16>(merge(m_scieb, data, mask) & kIrqSources);
            update_interrupts();
            break;
        case 0x20:
            // SCIPD is read only, but for the CPU source a program may raise.
            m_scipd = static_cast<u16>(m_scipd | (data & mask & kIrqCpu));
            update_interrupts();
            break;
        case 0x22:
            m_scipd = static_cast<u16>(m_scipd & ~(data & mask));
            update_interrupts();
            break;
        case 0x24:
        case 0x26:
        case 0x28: {
            u8& level = m_scilv[(address - kControlBase - 0x24) >> 1];
            level     = static_cast<u8>(merge(level, data, mask));
            update_interrupts();
            break;
        }
        case 0x2a:
            m_mcieb = static_cast<u16>(merge(m_mcieb, data, mask) & kIrqSources);
            update_interrupts();
            break;
        case 0x2c:
            m_mcipd = static_cast<u16>(m_mcipd | (data & mask & kIrqCpu));
            update_interrupts();
            break;
        case 0x2e:
            m_mcipd = static_cast<u16>(m_mcipd & ~(data & mask));
            update_interrupts();
            break;
        default:
            break;  // MIDI input (read only) and unused words
    }
}

// ---------------------------------------------------------------------------
// DMA
// ---------------------------------------------------------------------------

void ScspMednafen::run_dma()
{
    // A DMA into its own control register would start itself again.
    if (m_dma_running) {
        return;
    }
    m_dma_running = true;
    ++m_stats.dma_transfers;

    // All of it at once. DMEA, DRGA and DTLG are left as written: the chip
    // counts in copies of them.
    for (u32 i = 0; i < m_dma_length; ++i) {
        const u32 memory   = ((m_dma_memory + i) & 0x7ffffu) << 1;
        const u32 register_address = ((m_dma_register + i) & 0x7ffu) << 1;
        if (m_dma_to_memory) {
            // DGATE zeroes the data, but the read still happens.
            u16 value = register_read(register_address, 0xffff);
            if (m_dma_gate) {
                value = 0;
            }
            m_memory->scsp_write_word(memory, value);
        } else {
            u16 value = m_memory->scsp_read_word(memory);
            if (m_dma_gate) {
                value = 0;
            }
            register_write(register_address, value, 0xffff);
        }
    }

    m_dma_running = false;
    raise(kIrqDma);
}

// ---------------------------------------------------------------------------
// MIDI
// ---------------------------------------------------------------------------

u8 ScspMednafen::midi_flags() const
{
    u8 flags = 0;
    if (m_midi_in.empty()) flags |= kMidiInEmpty;
    if (m_midi_in.full()) flags |= kMidiInFull;
    if (m_midi_in_overflow) flags |= kMidiInOverflow;
    if (m_midi_out.empty()) flags |= kMidiOutEmpty;
    if (m_midi_out.full()) flags |= kMidiOutFull;
    return flags;
}

u8 ScspMednafen::midi_read()
{
    const bool had_data = !m_midi_in.empty();
    const u8   value    = m_midi_in.pop();
    if (had_data) {
        m_midi_in_overflow = false;
        if (m_midi_in.empty()) {
            m_scipd = static_cast<u16>(m_scipd & ~kIrqMidiIn);
            m_mcipd = static_cast<u16>(m_mcipd & ~kIrqMidiIn);
            update_interrupts();
        }
    }
    return value;
}

void ScspMednafen::midi_in(u8 value)
{
    if (m_midi_in.full()) {
        // Four bytes deep: a fifth is lost, and the chip says so.
        m_midi_in_overflow = true;
        ++m_stats.midi_in_dropped;
        return;
    }
    m_midi_in.push(value);
    ++m_stats.midi_in_bytes;
    raise(kIrqMidiIn);
}

void ScspMednafen::midi_write(u8 value)
{
    if (m_midi_out.full()) {
        return;  // lost, as on the chip
    }
    m_midi_out.push(value);
    m_scipd = static_cast<u16>(m_scipd & ~kIrqMidiOut);
    m_mcipd = static_cast<u16>(m_mcipd & ~kIrqMidiOut);
    update_interrupts();
}

void ScspMednafen::tick_midi_out()
{
    // The transmitter works a bit at a time, at less than a bit per sample.
    m_midi_bit_phase += kMidiBitsPerStep;
    if (m_midi_bit_phase < kMidiSamplesPerStep) {
        return;
    }
    m_midi_bit_phase -= kMidiSamplesPerStep;

    // An idle line takes the next byte straight away; the FIFO draining is an
    // interrupt, since there is then room for four more.
    if (m_midi_tx_bits == 0 && !m_midi_out.empty()) {
        m_midi_tx_byte = m_midi_out.pop();
        m_midi_tx_bits = kMidiBitsPerByte;
        if (m_midi_out.empty()) {
            raise(kIrqMidiOut);
        }
    }
    if (m_midi_tx_bits != 0 && --m_midi_tx_bits == 0) {
        ++m_stats.midi_out_bytes;
        if (m_midi_out_cb) {
            m_midi_out_cb(m_midi_tx_byte);
        }
    }
}

// ---------------------------------------------------------------------------
// Timers, slots and interrupts
// ---------------------------------------------------------------------------

void ScspMednafen::tick_timers()
{
    for (usize which = 0; which < m_timers.size(); ++which) {
        Timer& timer = m_timers[which];
        // Every 2^prescale samples, counted by the chip's own sample counter, so
        // a timer's ticks do not move when it is written.
        const u64 period_mask = (u64{1} << timer.prescale) - 1;
        if ((m_sample_count & period_mask) != 0) {
            continue;
        }
        if (timer.reload >= 0) {
            timer.count  = static_cast<u8>(timer.reload);
            timer.reload = -1;
        } else {
            ++timer.count;
        }
        // Free running: it goes on counting through 0xff and round again.
        if (timer.count == 0xff) {
            ++m_stats.timer_interrupts;
            raise(static_cast<u16>(kIrqTimerA << which));
        }
    }
}

// ---------------------------------------------------------------------------
// Envelopes
// ---------------------------------------------------------------------------

unsigned ScspMednafen::key_scale(usize slot) const
{
    // KRS 15 turns key scaling off; otherwise KRS plus the signed octave,
    // within 0..15.
    const std::array<u16, 16>& regs = m_slot_regs[slot];
    const int                  krs  = (regs[kSlotRegRelease] >> 10) & 0xf;
    if (krs == 0xf) {
        return 0;
    }
    const int octave = static_cast<int>(((regs[kSlotRegPitch] >> 11) & 0xfu) ^ 8u) - 8;
    return static_cast<unsigned>(std::clamp(krs + octave, 0, 15));
}

void ScspMednafen::run_envelope(usize slot, unsigned scale)
{
    const std::array<u16, 16>& regs  = m_slot_regs[slot];
    Voice&                     voice = m_voices[slot];

    const u16 rates = regs[kSlotRegRates];
    unsigned  base  = 0;
    switch (voice.phase) {
        case Phase::Attack:  base = rates & 0x1fu; break;
        case Phase::Decay1:  base = (rates >> 6) & 0x1fu; break;
        case Phase::Decay2:  base = (rates >> 11) & 0x1fu; break;
        case Phase::Release: base = regs[kSlotRegRelease] & 0x1fu; break;
    }
    const unsigned rate   = std::min(31u, base + scale);
    const u16      before = voice.level;

    // A rate of 0 holds the envelope, whatever the key scaling. An attack at 32
    // or more was done at key-on and never ticks.
    bool tick = base != 0 && envelope_ticks(rate, m_sample_count);
    if (voice.phase == Phase::Attack && base + scale >= 32) {
        tick = false;
    }
    if (tick) {
        const unsigned shift = envelope_shift(rate, m_sample_count);
        s32            level = before;
        if (voice.phase == Phase::Attack) {
            level += (~s32{before}) >> shift;  // -(level + 1) >> shift, rounded up
        } else {
            level += 16 >> shift;
        }
        voice.level = static_cast<u16>(std::clamp(level, 0, s32{kSilent}));
    }

    // Moving on is decided on the level from before this sample's step.
    const u16 release = regs[kSlotRegRelease];
    if (voice.phase == Phase::Decay1) {
        // DL, 32 levels, meets the envelope's top five bits.
        if (static_cast<unsigned>(before >> 5) == ((release >> 5) & 0x1fu)) {
            voice.phase = Phase::Decay2;
        }
    } else if (voice.phase == Phase::Attack) {
        // An attack ends at full level, or with LPSLNK on reaching the loop.
        const bool done = (release & kLoopLink) != 0 ? voice.in_loop : before == 0;
        if (done) {
            voice.phase = Phase::Decay1;
        }
    }
}

void ScspMednafen::apply_key(usize slot, unsigned scale)
{
    const std::array<u16, 16>& regs     = m_slot_regs[slot];
    Voice&                     voice    = m_voices[slot];
    const bool                 on       = (regs[kSlotRegKey] & kKeyOn) != 0;
    const bool                 released = voice.phase == Phase::Release;

    if (on && released) {
        // Key on: the waveform again from SA, the envelope into its attack from
        // the key-on level, or straight to full level for an instant attack.
        ++m_stats.slot_starts;
        const unsigned attack = regs[kSlotRegRates] & 0x1fu;
        voice                 = Voice{};
        voice.reading         = true;
        voice.phase           = Phase::Attack;
        voice.level           = attack + scale >= 32 ? u16{0} : kAttackStart;
    } else if (!on && !released) {
        voice.phase = Phase::Release;
    }
}

void ScspMednafen::monitor_slot(usize slot)
{
    // Latched as the slot runs, so a program sees the slot as it stood at the
    // start of its last sample: CA (bits 15..12 of where it reads; 0 once it has
    // stopped reading) in bits 10..7, SGC (the envelope phase) in 6..5 and EG
    // (the envelope's attenuation, before TL) in 4..0.
    const Voice&   voice = m_voices[slot];
    const unsigned ca    = voice.reading ? (voice.position >> 12) & 0xfu : 0u;
    m_monitor_data = static_cast<u16>((ca << 7) | (static_cast<unsigned>(voice.phase) << 5)
                                      | (envelope_output(slot) >> 5));
}

unsigned ScspMednafen::envelope_output(usize slot) const
{
    const std::array<u16, 16>& regs  = m_slot_regs[slot];
    const Voice&               voice = m_voices[slot];
    const bool held = voice.phase == Phase::Attack && (regs[kSlotRegRates] & kAttackHold) != 0;
    const bool bypassed = (regs[kSlotRegRelease] & kEgBypass) != 0;
    return held || bypassed ? 0u : voice.level;
}

void ScspMednafen::raise(u16 sources)
{
    m_scipd = static_cast<u16>(m_scipd | sources);
    m_mcipd = static_cast<u16>(m_mcipd | sources);
    update_interrupts();
}

void ScspMednafen::update_interrupts()
{
    // Towards the sound CPU: the highest level among the sources both pending
    // and enabled, a source's level being its bits in SCILV2..0. Sources 8 to 10
    // have no SCILV bits of their own and use source 7's.
    const u16 active = static_cast<u16>(m_scipd & m_scieb);
    int       level  = 0;
    for (unsigned source = 0; source < kIrqSourceCount; ++source) {
        if ((active & (1u << source)) == 0) {
            continue;
        }
        const unsigned bit          = std::min(source, 7u);
        const int      source_level = static_cast<int>(((m_scilv[0] >> bit) & 1u)
                                                       | (((m_scilv[1] >> bit) & 1u) << 1)
                                                       | (((m_scilv[2] >> bit) & 1u) << 2));
        level = std::max(level, source_level);
    }
    if (level != m_irq_level) {
        m_irq_level = level;
        if (m_irq_cb) {
            // Let go of what was driven, then drive the new level.
            m_irq_cb(0, false);
            if (level > 0) {
                m_irq_cb(level, true);
            }
        }
    }

    // Towards the host: any source pending and enabled.
    const bool main = (m_mcipd & m_mcieb) != 0;
    if (main != m_main_irq) {
        m_main_irq = main;
        if (m_main_irq_cb) {
            m_main_irq_cb(main);
        }
    }
}

// ---------------------------------------------------------------------------
// Slot playback
// ---------------------------------------------------------------------------

void ScspMednafen::follow_loop(Voice& voice, unsigned mode, u16 loop_start, u16 loop_end)
{
    // The comparisons are the address counter's own, 16 bits wide: a position
    // one short of the next sample is "position + 1" in that counter.
    const u16 reached = static_cast<u16>(voice.position + 1);

    // Turning round mirrors the whole position, fraction included: the chip
    // counts backwards by complementing its counter, not by stepping it down.
    const auto turn_round = [&voice](u32 mirror) {
        voice.position = static_cast<u16>(mirror - 1u - voice.position);
        voice.fraction = static_cast<u16>(kPhaseMask - voice.fraction);
        voice.backward = !voice.backward;
    };

    if (!voice.in_loop) {
        // From SA to the loop. The loop starts on reaching LSA, and a reverse
        // loop turns round there to play from its end back down.
        if (reached > loop_start) {
            voice.in_loop = true;
            if (mode == kLoopReverse) {
                turn_round(u32{loop_start} + loop_end);
            }
        }
        return;
    }

    if (!voice.backward) {
        if (reached <= loop_end) {
            return;
        }
        switch (mode) {
            case kLoopOff:
                voice.reading = false;
                break;
            case kLoopNormal:
                voice.position = static_cast<u16>(voice.position - loop_end + loop_start);
                break;
            case kLoopAlternate:
                // Turn round at the end, one sample short of it.
                turn_round(2u * loop_end);
                break;
            default:
                break;  // a reverse loop never plays forwards inside the loop
        }
        return;
    }

    if (reached > loop_start) {
        return;
    }
    if (mode == kLoopReverse) {
        voice.position = static_cast<u16>(voice.position + loop_end - loop_start);
    } else if (mode == kLoopAlternate) {
        turn_round(2u * loop_start);
    }
}

s16 ScspMednafen::play_slot(usize slot)
{
    const std::array<u16, 16>& regs   = m_slot_regs[slot];
    Voice&                     voice  = m_voices[slot];
    const u16                  key    = regs[kSlotRegKey];
    const unsigned             source = (key >> 7) & 3u;
    const u16                  flip   = kSbctlXor[(key >> 9) & 3u];

    // Noise, silence, and a slot that has stopped reading need no memory; SBCTL
    // applies to them all the same. (The noise source moves on in generate(),
    // after the pitch LFO has used it and before the amplitude LFO does.)
    u16 sample = static_cast<u16>((source == kSourceNoise ? (m_noise & 0xffu) << 8 : 0u) ^ flip);

    if (!voice.reading) {
        return static_cast<s16>(sample);
    }

    if (source == kSourceMemory) {
        // Two neighbouring samples, blended by the top six bits of the fraction.
        // The FM input moves where they are read, in 64ths of a sample: its
        // whole part shifts the position, its fraction adds to this one. The
        // chip places the second sample with the next slot's fraction instead
        // of this slot's, which only matters under FM.
        const s32 modulation = fm_input(slot);
        const s32 here       = modulation + (voice.fraction >> (kPhaseBits - 6));
        const s32 there      = modulation
                        + (m_voices[(slot + 1) % m_voices.size()].fraction >> (kPhaseBits - 6));
        // Bit 15 of the pitch word, left out of the manual: the offset from SA
        // wraps within the lowest power of two set in LEA's bits 10..7.
        u32            wave_mask = 0xffffffffu;
        const unsigned end_bits  = regs[kSlotRegLoopEnd] & 0x780u;
        if ((regs[kSlotRegPitch] & kShortWave) != 0 && end_bits != 0) {
            wave_mask = (1u << std::countr_zero(end_bits)) - 1u;
        }
        const u32 first_at = (u32{voice.position} + static_cast<u32>(wrap11(here >> 6))) & wave_mask;
        const u32 second_at = (u32{static_cast<u16>(voice.position + 1)} + static_cast<u32>(wrap11(there >> 6)))
                              & wave_mask;

        const u32 start = (u32{key & 0xfu} << 16) | regs[kSlotRegStart];
        s32       first;
        s32       second;
        if ((key & kPcm8) != 0) {
            first  = static_cast<s8>(m_memory->scsp_read_byte((start + first_at) & 0xfffffu)) * 256;
            second = static_cast<s8>(m_memory->scsp_read_byte((start + second_at) & 0xfffffu)) * 256;
        } else {
            // 16-bit data is word aligned: bit 0 of SA is not used.
            const u32 base = start >> 1;
            first  = static_cast<s16>(m_memory->scsp_read_word(((base + first_at) & 0x7ffffu) << 1));
            second = static_cast<s16>(m_memory->scsp_read_word(((base + second_at) & 0x7ffffu) << 1));
        }
        first  = static_cast<s16>(static_cast<u16>(first) ^ flip);
        second = static_cast<s16>(static_cast<u16>(second) ^ flip);
        const s32 weight = here & 0x3f;
        sample           = static_cast<u16>((first * (64 - weight) + second * weight) >> 6);
    }

    // The pitch: FNS is a 10-bit mantissa below an implicit 1, OCT a signed
    // octave from -8 to 7, so the step is 1.0 (in kPhaseBits of fraction) at
    // OCT 0, FNS 0. The pitch LFO is added to the mantissa, so a vibrato is the
    // same interval at every octave.
    const u16      pitch    = regs[kSlotRegPitch];
    const unsigned octave   = ((pitch >> 11) & 0xfu) ^ 8u;
    const s32      mantissa = static_cast<s32>(pitch_mantissa(pitch)) + pitch_lfo(slot);
    const u32      step     = (static_cast<u32>(mantissa) << octave) >> 4;
    u32            fixed  = (u32{voice.position} << kPhaseBits) | voice.fraction;
    fixed                 = voice.backward ? fixed - step : fixed + step;
    voice.position        = static_cast<u16>(fixed >> kPhaseBits);
    voice.fraction        = static_cast<u16>(fixed & kPhaseMask);

    return static_cast<s16>(sample);
}

// ---------------------------------------------------------------------------
// LFOs
// ---------------------------------------------------------------------------

void ScspMednafen::run_lfo(usize slot)
{
    Lfo&      lfo     = m_lfos[slot];
    const u16 control = m_slot_regs[slot][kSlotRegLfo];
    if (--lfo.wait == 0) {
        ++lfo.counter;
        // LFOF: bits 1..0 pick 8, 7, 6 or 5 times 128 samples, bits 4..2 halve
        // that, less four samples: 1020 samples a step at 0 (0.17 Hz over the 256
        // steps of a cycle) down to one at 31 (172.3 Hz).
        const unsigned frequency = (control >> 10) & 0x1fu;
        lfo.wait = static_cast<u16>((((8u - (frequency & 3u)) << 7) >> (frequency >> 2)) - 4u);
    }
    if ((control & kLfoReset) != 0) {
        lfo.counter = 0;
    }
}

s32 ScspMednafen::pitch_lfo(usize slot) const
{
    const std::array<u16, 16>& regs    = m_slot_regs[slot];
    const u16                  control = regs[kSlotRegLfo];
    const unsigned             depth   = (control >> 5) & 7u;  // PLFOS
    if (depth == 0) {
        return 0;
    }

    // Signed waveforms, -128 to 126.
    const unsigned counter = m_lfos[slot].counter;
    const s32      quarter = static_cast<s32>(counter & 0x3fu);
    s32            wave    = 0;
    switch ((control >> 8) & 3u) {
        case kLfoSaw:
            wave = static_cast<s8>(counter & 0xfeu);
            break;
        case kLfoSquare:
            wave = (counter & 0x80u) != 0 ? -128 : 126;
            break;
        case kLfoTriangle:
            // Up from 0 to 126 and back, then down to -128 and back.
            switch (counter >> 6) {
                case 0:  wave = quarter * 2; break;
                case 1:  wave = (63 - quarter) * 2; break;
                case 2:  wave = -2 - quarter * 2; break;
                default: wave = -128 + quarter * 2; break;
            }
            break;
        case kLfoNoise:
            wave = static_cast<s8>(m_noise & 0xfeu);
            break;
    }

    // The depth follows Sega's SCSP User's Manual (4.2.6, table 4.17): PLFOS 1
    // to 7 give ±7, 13.5, 27, 55, 112, 230 and 494 cents, which is the wave
    // reaching ±2^(PLFOS+1)/1024 of the pitch (494 cents is 768/1024 down). The
    // hardware model this core otherwise follows has half that, ±2^PLFOS/1024;
    // MAME follows the manual. The manual is taken until a recording of the
    // hardware says otherwise (SCSP.md, "Check against the official manual").
    wave = (wave * 2) >> (7u - depth);

    // The depth follows the pitch: scaled by the top bits of the mantissa, from
    // 64/64 to 127/64 of it, so the interval stays about the same across an
    // octave.
    const s32 scale = static_cast<s32>(pitch_mantissa(regs[kSlotRegPitch]) >> 4);
    return (scale * wave) >> 6;
}

unsigned ScspMednafen::amplitude_lfo(usize slot) const
{
    const u16      control = m_slot_regs[slot][kSlotRegLfo];
    const unsigned depth   = control & 7u;  // ALFOS
    if (depth == 0) {
        return 0;
    }

    // Attenuations, 0 to 254: at ALFOS 7 up to 23.8 dB, halving with each step down.
    const unsigned counter = m_lfos[slot].counter;
    unsigned       wave    = 0;
    switch ((control >> 3) & 3u) {
        case kLfoSaw:
            wave = counter & 0xfeu;
            break;
        case kLfoSquare:
            wave = (counter & 0x80u) != 0 ? 0xfeu : 0u;
            break;
        case kLfoTriangle:
            wave = ((counter & 0x80u) != 0 ? 0xffu - counter : counter) << 1;
            break;
        case kLfoNoise:
            wave = m_noise & 0xfeu;
            break;
    }
    return wave >> (7u - depth);
}

unsigned ScspMednafen::slot_time(usize slot) const
{
    return static_cast<unsigned>(((m_sample_count & 1u) << 5) | slot);
}

s32 ScspMednafen::fm_input(usize slot) const
{
    const u16      fm    = m_slot_regs[slot][kSlotRegFm];
    const unsigned depth = fm >> 12;  // MDL
    if (depth <= 4) {
        return 0;
    }
    // MDXSL and MDYSL are offsets from now: entry now + k holds the output of
    // the slot period 64 - k back (128 - k for k of 60 and up, which the
    // four-period delay has not reached yet).
    const unsigned now = slot_time(slot);
    const s32      x   = static_cast<s16>(m_sound_stack[(now + ((fm >> 6) & 0x3fu)) & 63u]);
    const s32      y   = static_cast<s16>(m_sound_stack[(now + (fm & 0x3fu)) & 63u]);
    // MDL 15 moves the read by up to 16 samples either way, each step down
    // halving that; the lowest bit is always clear.
    return (((x + y) * 64) >> (16u - depth)) & ~s32{1};
}

s16 ScspMednafen::level_slot(usize slot, s16 sample) const
{
    // One attenuation from the envelope, TL (four steps a unit) and the
    // amplitude LFO, saturating at silence; SDIR sends the sample out untouched.
    const u16 levels = m_slot_regs[slot][kSlotRegLevel];
    if ((levels & kSoundDirect) != 0) {
        return sample;
    }
    const unsigned attenuation =
        std::min(envelope_output(slot) + ((levels & 0xffu) << 2) + amplitude_lfo(slot), unsigned{kSilent});
    return static_cast<s16>(attenuate(sample, attenuation));
}

void ScspMednafen::mix_slot(usize slot, s16 output, s32& left, s32& right) const
{
    const s32 value = (s32{output} * m_slot_gain[slot]) >> 8;

    // The per-set gain can take a slot past 16 bits, hence the wide products.
    const u16       sends  = m_slot_regs[slot][kSlotRegOutput];
    const SendGains direct = send_gains((sends >> 13) & 7u, (sends >> 8) & 0x1fu);
    left += static_cast<s32>((s64{value} * direct.left) >> 14);
    right += static_cast<s32>((s64{value} * direct.right) >> 14);
}

void ScspMednafen::send_to_dsp(usize slot, s16 output)
{
    const u16      mix   = m_slot_regs[slot][kSlotRegMix];
    const unsigned level = mix & 7u;  // IMXL
    if (level == 0) {
        return;
    }
    // At IMXL 7 the sample lands in the top 16 of MIXS's 20 bits; each step
    // down halves it. The inputs wrap rather than saturate.
    const s32 value = (s32{output} * m_slot_gain[slot]) >> 8;
    u32&      input = m_mixs[(mix >> 3) & 0xfu];  // ISEL
    input           = (input + static_cast<u32>((value * 16) >> (7u - level))) & 0xfffffu;
}

// ---------------------------------------------------------------------------
// Effects DSP
// ---------------------------------------------------------------------------

void ScspMednafen::run_dsp()
{
    Dsp&      dsp       = m_dsp;
    const u32 ring_mask = (0x2000u << m_rbl) - 1u;

    for (const u64 op : m_mpro) {
        const auto field = [op](unsigned shift, unsigned bits) {
            return static_cast<unsigned>((op >> shift) & ((u64{1} << bits) - 1));
        };
        const unsigned tra        = field(56, 7);
        const bool     twt        = field(55, 1) != 0;
        const unsigned twa        = field(48, 7);
        const bool     xsel       = field(47, 1) != 0;
        const unsigned ysel       = field(45, 2);
        const unsigned ira        = field(38, 6);
        const bool     iwt        = field(37, 1) != 0;
        const unsigned iwa        = field(32, 5);
        const bool     table      = field(31, 1) != 0;
        const bool     mwt        = field(30, 1) != 0;
        const bool     mrt        = field(29, 1) != 0;
        const bool     ewt        = field(28, 1) != 0;
        const unsigned ewa        = field(24, 4);
        const bool     adrl       = field(23, 1) != 0;
        const bool     frcl       = field(22, 1) != 0;
        const unsigned shift_mode = field(20, 2);  // SHFT1, SHFT0
        const bool     yrl        = field(19, 1) != 0;
        const bool     negb       = field(18, 1) != 0;
        const bool     zero       = field(17, 1) != 0;
        const bool     bsel       = field(16, 1) != 0;
        const unsigned cra        = field(9, 6);
        const bool     nofl       = field(8, 1) != 0;
        const unsigned masa       = field(2, 5);
        const bool     adrgb      = field(1, 1) != 0;
        const bool     nxadr      = field(0, 1) != 0;

        // INPUTS latches the MEMS, MIXS or EXTS word IRA names. IRA 0x32 and
        // up name nothing, and the latch keeps what it held.
        if (ira < 0x20) {
            dsp.inputs = m_mems[ira];
        } else if (ira < 0x30) {
            dsp.inputs = (m_mixs[ira & 0xfu] << 4) & 0xffffffu;
        } else if (ira < 0x32) {
            dsp.inputs = u32{m_exts[ira & 1u]} << 8;
        }
        const s32 inputs = sign_extend(dsp.inputs, 24);

        // Y, a 13-bit fraction, is chosen before YRL moves Y_REG on. The last
        // choice is Y_REG's bits 15..4, which is never negative.
        u32 y_bits = 0;
        switch (ysel) {
            case 0:  y_bits = dsp.frc; break;
            case 1:  y_bits = m_coef[cra]; break;
            case 2:  y_bits = (dsp.y >> 11) & 0x1fffu; break;
            default: y_bits = (dsp.y >> 4) & 0x0fffu; break;
        }
        const s32 y = sign_extend(y_bits, 13);
        if (yrl) {
            dsp.y = dsp.inputs;
        }

        // The shifter works on the last step's accumulator: SHFT0 or SHFT1
        // alone double it, and it saturates to 24 bits unless SHFT1 is set.
        s32 shifted = sign_extend(dsp.accumulator, 26);
        if (shift_mode == 1 || shift_mode == 2) {
            shifted *= 2;
        }
        if ((shift_mode & 2u) == 0) {
            shifted = std::clamp(shifted, -0x800000, 0x7fffff);
        }
        const u32  shifter = static_cast<u32>(shifted) & 0xffffffu;
        const bool low_bits = shift_mode == 3;  // FRCL and ADRL take the low end

        if (frcl) {
            dsp.frc = static_cast<u16>(low_bits ? shifter & 0xfffu : shifter >> 11);
        }

        // X times Y, plus B, into the 26-bit accumulator.
        const s32 temp = sign_extend(m_temp[(tra + dsp.ring_offset) & 0x7fu], 24);
        const s32 x    = xsel ? inputs : temp;
        u32       b    = bsel ? dsp.accumulator : static_cast<u32>(temp);
        if (negb) {
            b = 0u - b;
        }
        if (zero) {
            b = 0;
        }
        const u32 product = static_cast<u32>((s64{y} * x) >> 12);
        dsp.accumulator   = (product + b) & 0x3ffffffu;

        if (ewt) {
            m_efreg[ewa] = static_cast<u16>(shifter >> 8);
        }
        if (twt) {
            m_temp[(twa + dsp.ring_offset) & 0x7fu] = shifter;
        }
        if (iwt) {
            m_mems[iwa] = dsp.read_value;
        }

        // The memory access the last step asked for happens now, a read ahead
        // of a write, at the address that step worked out.
        if (dsp.read_pending != 0) {
            const u16 word = m_memory->scsp_read_word(dsp.access_address << 1);
            dsp.read_value   = dsp.read_pending == 2 ? u32{word} << 8 : dsp_float_to_int(word);
            dsp.read_pending = 0;
        } else if (dsp.write_pending) {
            m_memory->scsp_write_word(dsp.access_address << 1, dsp.write_value);
            dsp.write_pending = false;
        }

        // This step's address, in words: MADRS, the next word with NXADR,
        // ADRS_REG with ADRGB, then outside TABLE the ring offset, kept within
        // the ring buffer; RBP places the buffer in memory.
        u16 address = static_cast<u16>(m_madrs[masa] + (nxadr ? 1u : 0u));
        if (adrgb) {
            address = static_cast<u16>(address + sign_extend(dsp.adrs, 12));
        }
        if (!table) {
            address = static_cast<u16>((address + dsp.ring_offset) & ring_mask);
        }
        dsp.access_address = (address + (u32{m_rbp} << 12)) & 0x7ffffu;
        if (mrt) {
            dsp.read_pending = nofl ? 2 : 1;
        }
        if (mwt) {
            dsp.write_pending = true;
            dsp.write_value   = nofl ? static_cast<u16>(shifter >> 8) : dsp_int_to_float(shifter);
        }

        if (adrl) {
            dsp.adrs = static_cast<u16>(low_bits ? shifter >> 12
                                                 : (static_cast<u32>(inputs) >> 16) & 0xfffu);
        }
    }

    // The ring offset counts down through the buffer, one word a sample. A
    // 64K-word buffer wraps the 16-bit counter the same way.
    if (dsp.ring_offset == 0) {
        dsp.ring_offset = static_cast<u16>(0x2000u << m_rbl);
    }
    --dsp.ring_offset;
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

void ScspMednafen::generate(s16* output, u32 frames)
{
    for (u32 frame = 0; frame < frames; ++frame) {
        tick_midi_out();
        tick_timers();
        raise(kIrqSample);

        // The DSP works on what the slots sent it over the last sample, before
        // they send again; its EFREG results go out with this one.
        run_dsp();
        m_mixs = {};

        // Every slot runs every sample, in two passes as on the chip. First the
        // envelopes, a pending KYONEX and the loops, for all 32 slots; then
        // each slot's waveform and its share of the output. So a slot's FM
        // read already sees the next slot keyed on or turned round.
        for (usize slot = 0; slot < m_voices.size(); ++slot) {
            const std::array<u16, 16>& regs  = m_slot_regs[slot];
            Voice&                     voice = m_voices[slot];

            // Once the envelope has all but died away the slot stops reading
            // memory, unless the envelope is bypassed.
            if (voice.level >= kReadCutoff && (regs[kSlotRegRelease] & kEgBypass) == 0) {
                voice.reading = false;
            }
            const unsigned scale = key_scale(slot);
            run_envelope(slot, scale);
            if (m_key_execute) {
                apply_key(slot, scale);
            }
            follow_loop(voice, (regs[kSlotRegKey] >> 5) & 3u, regs[kSlotRegLoopStart],
                        regs[kSlotRegLoopEnd]);
        }
        m_key_execute = false;

        s64 left  = 0;
        s64 right = 0;
        for (usize slot = 0; slot < m_voices.size(); ++slot) {
            const std::array<u16, 16>& regs = m_slot_regs[slot];
            if (slot == m_monitor_slot) {
                monitor_slot(slot);
            }

            // The pitch LFO bends this sample's step from the LFO and noise
            // source as they stood; both move on before the amplitude LFO is read.
            const s16 sample = play_slot(slot);
            run_lfo(slot);
            m_noise = (m_noise >> 1) | ((((m_noise >> 5) ^ m_noise) & 1u) << 16);
            const s16 slot_output = level_slot(slot, sample);

            // The slot's output reaches the sound stack four slot periods
            // later, into the entry of the slot period it came from, unless
            // that slot has STWINH set.
            const unsigned now     = slot_time(slot);
            const unsigned written = (now - 4u) & 63u;
            if ((m_slot_regs[written & 31u][kSlotRegLevel] & kStackWriteInhibit) == 0) {
                m_sound_stack[written] = m_stack_delay[3];
            }
            m_stack_delay[3] = m_stack_delay[2];
            m_stack_delay[2] = m_stack_delay[1];
            m_stack_delay[1] = m_stack_delay[0];
            m_stack_delay[0] = static_cast<u16>(slot_output);

            send_to_dsp(slot, slot_output);

            s32 slot_left  = 0;
            s32 slot_right = 0;
            mix_slot(slot, slot_output, slot_left, slot_right);

            // The effect returns ride on the slots' EFSDL/EFPAN: slot n carries
            // EFREG n, slots 16 and 17 EXTS 0 and 1, the rest nothing.
            const s32 effect = slot < 16   ? static_cast<s16>(m_efreg[slot])
                               : slot < 18 ? static_cast<s16>(m_exts[slot - 16])
                                           : 0;
            if (effect != 0) {
                const u16       out   = regs[kSlotRegOutput];
                const SendGains gains = send_gains((out >> 5) & 7u, out & 0x1fu);
                slot_left += (effect * gains.left) >> 14;
                slot_right += (effect * gains.right) >> 14;
            }
            left += slot_left;
            right += slot_right;
        }

        const s64 master  = master_gain(m_mvol);
        const s32 out_l   = static_cast<s32>(std::clamp<s64>((left * master) >> 8, -32768, 32767));
        const s32 out_r   = static_cast<s32>(std::clamp<s64>((right * master) >> 8, -32768, 32767));
        output[frame * 2]     = static_cast<s16>(out_l);
        output[frame * 2 + 1] = static_cast<s16>(out_r);

        m_stats.peak_output = std::max({m_stats.peak_output, std::abs(out_l), std::abs(out_r)});
        ++m_sample_count;
        ++m_stats.samples;
    }
}

// ---------------------------------------------------------------------------
// Save states
// ---------------------------------------------------------------------------

void ScspMednafen::serialize(Archive& ar)
{
    ar.raw(m_slot_regs);
    for (Voice& voice : m_voices) {
        // Field by field: the struct has padding, which must not reach the file.
        ar.raw(voice.position);
        ar.raw(voice.fraction);
        ar.raw(voice.backward);
        ar.raw(voice.in_loop);
        ar.raw(voice.reading);
        ar.raw(voice.phase);
        ar.raw(voice.level);
    }
    for (Lfo& lfo : m_lfos) {
        ar.raw(lfo.counter);
        ar.raw(lfo.wait);
    }
    ar.raw(m_key_execute);
    ar.raw(m_noise);

    ar.raw(m_mvol);
    ar.raw(m_dac18);
    ar.raw(m_mem4mb);
    ar.raw(m_rbp);
    ar.raw(m_rbl);
    ar.raw(m_monitor_slot);
    ar.raw(m_monitor_data);

    ar.raw(m_midi_in);
    ar.raw(m_midi_out);
    ar.raw(m_midi_in_overflow);
    ar.raw(m_midi_tx_byte);
    ar.raw(m_midi_tx_bits);
    ar.raw(m_midi_bit_phase);

    ar.raw(m_dma_memory);
    ar.raw(m_dma_register);
    ar.raw(m_dma_length);
    ar.raw(m_dma_to_memory);
    ar.raw(m_dma_gate);

    ar.raw(m_timers);

    ar.raw(m_scieb);
    ar.raw(m_scipd);
    ar.raw(m_mcieb);
    ar.raw(m_mcipd);
    ar.raw(m_scilv);
    ar.raw(m_irq_level);
    ar.raw(m_main_irq);

    ar.raw(m_sound_stack);
    ar.raw(m_stack_delay);
    ar.raw(m_coef);
    ar.raw(m_madrs);
    ar.raw(m_mpro);
    ar.raw(m_temp);
    ar.raw(m_mems);
    ar.raw(m_mixs);
    ar.raw(m_efreg);
    ar.raw(m_exts);
    ar.raw(m_dsp.inputs);
    ar.raw(m_dsp.accumulator);
    ar.raw(m_dsp.frc);
    ar.raw(m_dsp.y);
    ar.raw(m_dsp.adrs);
    ar.raw(m_dsp.ring_offset);
    ar.raw(m_dsp.access_address);
    ar.raw(m_dsp.read_pending);
    ar.raw(m_dsp.write_pending);
    ar.raw(m_dsp.write_value);
    ar.raw(m_dsp.read_value);

    ar.raw(m_sample_count);
    ar.raw(m_slot_gain);
}

}  // namespace sm2::hw
