#pragma once
#include <windows.h>
#include <string>
namespace preyvr::dll {
// **Psi Aim Hand: which hand aims psi powers.** The left trigger is always
// Prey's own psi button (MoveLane): Prey selects, targets and casts. 0, right,
// leaves Prey's aim, which the aim takeover points along the right hand; 1,
// left, aims from the left index finger by changing the ray Prey's targeting
// reads while it runs (psi.hand).
DWORD SetPsiAimHand(unsigned hand);
unsigned PsiAimHand();
// A power is being aimed along the left hand now: Prey's targeting read the
// left ray within the last 100 ms. The left aim laser shows it.
bool PsiAimingLeftHand();
std::string PsiMedkitReport();
}
