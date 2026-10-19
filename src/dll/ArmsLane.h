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

// Game thread (input drain), once per gameplay frame: the holster state, and a
// requested draw carried out with the native equip.
void UpdateArmsLane(const GameplayPoseFrame& frame, bool valid);
// No weapon drawn and one to draw (holstered), nothing carried, no cinematic.
bool ArmsHolstered();
// The input lane: the right grip asks for the last weapon again.
void RequestDrawWeapon();
std::string ArmsReport();

// arms.* and mem.* verbs; false if the verb is not one of them.
bool ExecuteArmsCommand(const std::vector<std::string>& args, std::ostringstream& out);

// The joint the character's attachment `name` is bound to (arms.att's list:
// the attachment manager's array at character+0x38, the joint at +0x15C), or
// -1. Reads only, guarded; any thread.
int AttachmentJoint(std::uint64_t character, std::string_view name);

}  // namespace preyvr::dll
