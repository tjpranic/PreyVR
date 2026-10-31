#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

// **The raw-memory research verbs** (docs/TUNING-COMMANDS.md): mem.peek, scan,
// deep and str read any address, and mem.watch, hits and stack drive the
// hardware breakpoints (DebugWatch.h). Moved out of ArmsLane on 2026-10-24;
// their replies are unchanged.
namespace preyvr::dll {

// mem.* verbs; false if the verb is not one of them.
bool ExecuteMemoryCommand(const std::vector<std::string>& args, std::ostringstream& out);

// The address the verbs take: "player", "arms" (the IK's character), "dll"
// (PreyDll's base) or hex, plus an optional "+hex" offset. False for text that
// is none of those, or that resolves to 0. arms.att takes it too.
bool ResolveAddress(const std::string& text, std::uint64_t& value);

}  // namespace preyvr::dll
