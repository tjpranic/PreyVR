#pragma once
#include "controls/input/XrInput.h"
#include "controls/input/HapticPolicy.h"
#include <string>
namespace preyvr::dll {
void QueueHaptic(Hand,haptics::Event,const TrackingFrame&);
void SetHapticsEnabled(unsigned);
unsigned HapticsEnabled();
void SetHapticStrength(unsigned);
unsigned HapticStrength();
std::string HapticsReport();
}
