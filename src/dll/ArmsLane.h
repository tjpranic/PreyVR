#pragma once

#include <windows.h>

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The first-person arms when no weapon is drawn (holstered, carrying).
namespace preyvr::dll {

struct GameplayPoseFrame;

// Main thread (input drain), once per gameplay frame: the holster state, and a
// requested draw carried out with the native equip.
void UpdateArmsLane(const GameplayPoseFrame& frame, bool valid);
// No weapon drawn and one to draw (holstered), nothing carried, no cinematic.
bool ArmsHolstered();
// The input lane: the right grip asks for the last weapon again.
void RequestDrawWeapon();
std::string ArmsReport();

// arms.* verbs; false if the verb is not one of them. (The mem.* verbs are
// MemoryCommands'.)
bool ExecuteArmsCommand(const std::vector<std::string>& args, std::ostringstream& out);

// The character's attachment `name` (arms.att's list: the attachment manager's
// array at character+0x38, each name at +0x10), or 0. Reads only, guarded; any
// thread.
std::uint64_t FindAttachment(std::uint64_t character, std::string_view name);
// The joint that attachment is bound to (+0x15C), or -1.
int AttachmentJoint(std::uint64_t character, std::string_view name);
// Every attachment of `character` as " name@joint" items, for a log line
// (ik.skel's header); " unread" when the list cannot be read.
std::string AttachmentListText(std::uint64_t character);

}  // namespace preyvr::dll
