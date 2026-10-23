#pragma once
#include "preyvr/BodyEquipment.h"
#include <string>
namespace preyvr::dll {
struct TrackingFrame;
struct GameplayPoseFrame;
void UpdateHolsterInput(const TrackingFrame&,bool valid);
bool HolsterOwnsGrip();
void UpdateBodyEquipment(const GameplayPoseFrame&,bool valid);
void ClearHolsters();
void RecordWristLayerFrame();
std::string BodyEquipmentReport();
}
