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
// The protocol: one command a line on stdin, one answer on stdout.
//
//   step N STEER ACCEL BRAKE IN0 IN1
//       Hold the controls N frames and run them. STEER -1 (left) to 1 (right),
//       ACCEL and BRAKE 0 to 1, IN0 and IN1 the two button ports in hex, active
//       low (FF: nothing pressed). Answer: "ok FRAME" then every byte written to
//       the drive board meanwhile, in hex, on the same line.
//   obs W H
//       The picture, scaled to W x H. Answer: "obs W H", a new line, then
//       W * H * 3 bytes, RGB, row by row.
//   ram OFFSET LENGTH
//       The host CPU's work RAM (hex offset and length). Answer: "ram LENGTH",
//       a new line, then the bytes.
//   mem ADDRESS LENGTH
//       The same by the i960's address (hex): the work RAM at 00500000 or the
//       scratch RAM at 00200000. Answer: "mem LENGTH" (0 elsewhere), the bytes.
//   tel
//       The telemetry, one JSON line (see src/telemetry), or "tel none".
//   save PATH / load PATH
//       A save state. Answer: "ok" or "err".
//   quit
#include "agent.h"

#include "core/log.h"
#include "core/types.h"
#include "hw/model2_machine_base.h"
#include "hw/model2_softrender.h"
#include "hw/sound_board.h"
#include "rom/game.h"
#include "telemetry/telemetry.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace sm2 {
namespace {

/// A control's value on its channel, `fraction` 0..1 of its travel.
u8 channel_value(const rom::AnalogChannel& channel, float fraction)
{
    fraction         = std::clamp(fraction, 0.0f, 1.0f);
    const float span = static_cast<float>(channel.maximum - channel.minimum);
    const float v    = channel.reverse ? static_cast<float>(channel.maximum) - fraction * span
                                       : static_cast<float>(channel.minimum) + fraction * span;
    return static_cast<u8>(v + 0.5f);
}

void set_controls(hw::Inputs& inputs, const rom::GameSpec& game, float steer, float accel,
                  float brake)
{
    for (usize i = 0; i < game.analog.size(); ++i) {
        const rom::AnalogChannel& channel = game.analog[i];
        switch (channel.control) {
            case rom::AnalogControl::Steer:
                inputs.analog[i] = channel_value(channel, 0.5f + 0.5f * steer);
                break;
            case rom::AnalogControl::Accel: inputs.analog[i] = channel_value(channel, accel); break;
            case rom::AnalogControl::Brake: inputs.analog[i] = channel_value(channel, brake); break;
            default: break;
        }
    }
}

}  // namespace

int run_agent(hw::Model2MachineBase& machine, hw::SoundBoard* sound, const rom::GameSpec& game)
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

    // Controls at rest, as a cabinet nobody touches.
    set_controls(machine.inputs(), game, 0.0f, 0.0f, 0.0f);

    char line[1024];
    while (std::fgets(line, sizeof(line), stdin) != nullptr) {
        char command[16] = {};
        if (std::sscanf(line, "%15s", command) != 1) {
            continue;
        }
        if (std::strcmp(command, "quit") == 0) {
            break;
        }
        if (std::strcmp(command, "step") == 0) {
            unsigned n = 1, in0 = 0xff, in1 = 0xff;
            float    steer = 0.0f, accel = 0.0f, brake = 0.0f;
            std::sscanf(line, "%*s %u %f %f %f %x %x", &n, &steer, &accel, &brake, &in0, &in1);
            hw::Inputs& inputs = machine.inputs();
            set_controls(inputs, game, steer, accel, brake);
            inputs.in0 = static_cast<u8>(in0);
            inputs.in1 = static_cast<u8>(in1);
            std::string drive;
            for (unsigned i = 0; i < n; ++i) {
                machine.run_frame();
                ++frames;
                if (sound != nullptr) {
                    sound->clear_pending_samples();
                }
                    char hex[4];
                    std::snprintf(hex, sizeof(hex), " %02X", b);
                    drive += hex;
                }
            }
            std::printf("ok %llu%s\n", static_cast<unsigned long long>(frames), drive.c_str());
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
            if (decoder && decoder->decode({machine.work_ram(), machine.scratch_ram()}, frames, tel)) {
                const hw::Inputs& used = machine.inputs();
                for (usize i = 0; i < tel.analog.size(); ++i) {
                    tel.analog[i] = used.analog[i];
                }
                tel.in0 = used.in0;
                tel.in1 = used.in1;
                const std::string json = telemetry::to_json(tel);
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
                std::printf(ok ? "ok\n" : "err\n");
            }
        } else {
            std::printf("err unknown command\n");
        }
        std::fflush(stdout);
    }
    return 0;
}

}  // namespace sm2
