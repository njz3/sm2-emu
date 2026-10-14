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
// --agent: the machine driven over stdin/stdout by another program, a learning
// agent or a force feedback bench. No window, no sound, no pacing: frames run
// as fast as the host allows, between the agent's commands. The protocol is in
// agent.cpp.
#pragma once

#include "osd/input.h"

namespace sm2 {
namespace hw {
class Model2MachineBase;
class SoundBoard;
}  // namespace hw
namespace rom {
struct GameSpec;
}  // namespace rom

struct AgentOptions {
    /// --agent-device: the controllers (a wheel, vJoy) are read through SDL as
    /// in a window, under the agent's holds, and the force feedback goes to the
    /// wheel. Otherwise a virtual wheel follows the held steering and the force
    /// feedback is only recorded.
    bool device = false;

    /// The wheel settings of the ini: force feedback strength, the board's DIP...
    osd::Input::WheelSettings wheel;
};

/// Serve the agent's commands until "quit" or the end of stdin; the exit code.
int run_agent(hw::Model2MachineBase& machine, hw::SoundBoard* sound, const rom::GameSpec& game,
              const AgentOptions& options);

}  // namespace sm2
