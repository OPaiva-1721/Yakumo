#pragma once

// Developer tools: the command file that reads and writes guest memory from a
// script (debug_console.hpp). They exist only
// in developer builds: MHP3RD_DEBUG_MENU is off in release builds, which then
// compile none of this, and a developer build shows them only when the
// environment variable MHP3RD_DEBUG_MENU=1 is set.
//
// Nothing here writes guest memory directly. A request is queued and runs at
// the game's flip, between two game frames, on the thread that runs the game
// (debug::frame from present_frame).
// While ad hoc play is active the tools refuse to write anything, so a test
// never reaches another player's game.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace psprecomp {
class Runtime;
}

namespace mhp3rd::game {
class Ram;
}

namespace mhp3rd::debug {

using game::Ram;

// Compiled in and switched on for this run (MHP3RD_DEBUG_MENU=1).
[[nodiscard]] bool enabled();

// Whether writes are refused right now, and why ("" when they are allowed):
// during ad hoc play.
[[nodiscard]] std::string blocked_reason();

// Queues a change to guest memory. It runs between two game frames: at once
// when the menu calls it (the menu runs at the flip), otherwise at the next
// flip. `change` returns the line to log, what it did; `what` names it in the
// log when it is refused.
void request(std::string what, std::function<std::string(Ram &)> change);

// Reads guest memory now. Only for the menu, which runs at the flip too (see
// hle_media.cpp), and never for writing.
void read(const std::function<void(const Ram &)> &reader);

// At the flip, on the game's thread: runs queued requests and reads the
// command file.
void frame(psprecomp::Runtime &runtime);

// Logs a "[debug] ..." line.
void log(const std::string &line);
// The last lines logged, oldest first.
[[nodiscard]] std::vector<std::string> recent_log();

} // namespace mhp3rd::debug
