#pragma once

#include <windows.h>
#include <string>
#include "preyvr/RigOwnership.h"
#include "preyvr/VrMath.h"

// The equipped weapon, observed: which rig and attachment it is (revalidated on
// every read), where its muzzle sits relative to the grip, and the engine's own
// firing position for each shot. Nothing here writes to the weapon.
//
// The attachment is captured on `CArkWeapon::AttachToHand` (R-024) from
// `CArkWeapon+0x2B0`, and its model-space mount is read through
// `GetAttAbsoluteDefault` (attachment vtable index 10; R-024's AddBinding at
// +0xD8 is index 27, which aligns the table). R-088 records why writing that mount
// (`SetAttAbsoluteDefault`) displaces the weapon relative to the animation rather
// than placing it; the controller-driven mount writer that relied on it was
// superseded by the IK lane and removed on 2026-10-23.
namespace preyvr::dll {

using EquippedRig = RigIdentity;
// Revalidates selected equipment and the attachment -> manager -> character
// chain on every read. A skeleton asset signature alone is not ownership.
bool TryGetEquippedRig(std::uintptr_t player, EquippedRig& out);
// Bumps on every successful local attach; lets callers notice a re-equip
// without reading the rig.
std::uint64_t WeaponEquipGeneration();

// --- barrel calibration -----------------------------------------------------
//
// Derives where the weapon's muzzle sits relative to the tracked grip, from a
// real firing-position sample the passive observer already captures. That pair
// -- the engine's own muzzle and our grip, at one instant -- is the only
// evidence available for the offset, and it beats any authored guess: an
// attachment default is not a barrel axis and neither is the model's +Y.
//
// Fire once with the weapon steady, then calibrate. Refuses a sample that used
// the native camera fallback (a blocked safety ray, so not the muzzle at all),
// one belonging to a different weapon, or an implausible reach.
DWORD CalibrateWeaponBarrel();
// The calibrated muzzle for the current grip. False when uncalibrated, or when
// the weapon has changed since -- a different weapon has a different muzzle.
bool TryGetMuzzleFromGrip(const Pose& gripWorld, std::uint64_t currentEquipGeneration, Vec3& out);
unsigned long long WeaponBarrelCalibrations();
int WeaponBarrelOffsetMillimetres(unsigned int axis);

// Passive firing-position observer. Reports a matched sample's actual native
// origin, fallback flag and separation from aim/grip; never replaces firing.
std::string WeaponMuzzleAlignmentReport();
// Installs that observer on its own (aim.shot needs it without the IK lane).
bool EnsureFiringPositionHook();

// Installs the hook on `CArkWeapon::AttachToHand` (R-024), which captures the
// equipped weapon's `IAttachment*` from `CArkWeapon+0x2B0`, and the firing-position
// observer. Passive: nothing is written.
DWORD SetWeaponAttachmentObserving(unsigned int enabled);

// The captured attachment, or 0. Session-specific: re-equip a weapon to refresh it
// rather than carrying one between runs.
unsigned long long WeaponAttachmentPointer();
// The bone the resolved attachment hangs from (`IAttachment+0x15C`, R-102) and
// its binding's simulation flags (`+8+0x28` enabled, `+8+0x2B` redirect) -- a
// spring on the weapon binding lags it behind a fast wrist and, with redirect,
// writes back into the pose. -1 / 0 when nothing is resolved.
int WeaponAttachmentJointIndex();
unsigned int WeaponAttachmentSimulationFlags();

// The attachment's model-space mount as read through `GetAttAbsoluteDefault` at
// the last equip, in millimetres. 0 until a mount has been read.
int WeaponMountPositionMillimetres(unsigned int axis);   // 0 x, 1 y, 2 z

// Whether the mount above was read at the last equip.
unsigned int WeaponBaselineCaptured();

} // namespace preyvr::dll
