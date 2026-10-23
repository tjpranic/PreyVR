// The command channel's replies, pinned before its verb chains become a table:
// for each command line below, the exact reply and how the channel counts it --
// processed, rejected, or neither (the verbs a lane's own dispatcher answers).
// This is characterisation: a reply that looks odd is pinned as it is today, and
// changing one is a decision about the channel, not a fix to this test.
//
// A reply that ends in a lane's report is pinned up to the report, and the rest
// must be that report as the lane gives it straight after. What is pinned is
// which report a verb appends, not the lane's own fields. Rows run in order on
// one process's state; a row that changes a setting is followed by one that
// puts the default back.
//
// Offline and inert: PreyDll is never loaded, MinHook is never initialised, the
// log is captured, and OpenXR is delay-loaded from a directory without the
// loader, so a stray call fails instead of finding a runtime. Every verb here
// was read to the end of its handler. The ones left out, and why, follow the
// table. Including the TU exposes Execute, Split and the counters to this test.
#include "../src/dll/CommandChannel.cpp"
#include "MinHookInit.h"
#include "DllTestSupport.h"
#include "TestSupport.h"
#include <tlhelp32.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

PREYVR_TEST_CAPTURE_LOG()
PREYVR_TEST_STUB_MINHOOK()
// The logger's other entry points. The command file test uses this directory.
std::filesystem::path preyvr::lifecycle::LogPath()
{
    return std::filesystem::temp_directory_path() / L"PreyVR-command-channel-tests" / L"PreyVR.log";
}
void preyvr::lifecycle::ResetLog() {}

namespace {
using namespace preyvr::dll;
using preyvr::test::gLines;
using preyvr::test::Require;

enum Counted { processed, rejected, neither };
using Tail = std::string (*)();
struct Case {
    const char* line;
    const char* reply;   // what the channel writes before the tail
    Tail tail;           // the report it appends, read back afterwards
    Counted counted;
};

// Tails of more than one report, or of a report and the channel's text after it.
std::string DualReports() { return DualSceneReport() + DualOverlayReport(); }
std::string WorldReticleReports() { return ReticleWorldReport() + " scene:" + SceneQueryReport(); }
std::string VrOptionsReports() { return VrOptionsReport() + PsychoscopeReport() + BodyEquipmentReport(); }
std::string InventoryLayerReports()
{
    return HudLayerReport() + " layerFrames=" + std::to_string(InventoryLayerFrameCount());
}

// One line, as the command file step runs it.
std::string Run(const char* line)
{
    std::ostringstream out;
    Execute(Split(line), out);
    return out.str();
}

const Case kCases[] = {
    // Not a verb, or a verb short of the arguments it needs: rejected, and
    // counted as rejected. Verbs are case-sensitive; allowlisted cvars are not.
    {"bogus", "result=rejected verb=\"bogus\" detail=not_a_channel_verb_and_not_allowlisted", nullptr, rejected},
    {"VR.STATUS", "result=rejected verb=\"VR.STATUS\" detail=not_a_channel_verb_and_not_allowlisted", nullptr,
     rejected},
    {"vr.status2", "result=rejected verb=\"vr.status2\" detail=not_a_channel_verb_and_not_allowlisted", nullptr,
     rejected},
    {"use.find", "result=rejected verb=\"use.find\" detail=not_a_channel_verb_and_not_allowlisted", nullptr, rejected},
    {"hud.setvar path", "result=rejected verb=\"hud.setvar\" detail=not_a_channel_verb_and_not_allowlisted", nullptr,
     rejected},
    {"xr.runtime", "result=rejected verb=\"xr.runtime\" detail=not_a_channel_verb_and_not_allowlisted", nullptr,
     rejected},
    {"console", "result=rejected verb=\"console\" detail=not_a_channel_verb_and_not_allowlisted", nullptr, rejected},
    {"move.bind 1", "result=rejected verb=\"move.bind\" detail=not_a_channel_verb_and_not_allowlisted", nullptr,
     rejected},
    {"hud.fit", "result=rejected verb=\"hud.fit\" detail=not_a_channel_verb_and_not_allowlisted", nullptr, rejected},

    // ExecutePose. Its verbs, like every lane dispatcher's below, count as neither.
    {"use.report", "use.report result=0", UseReport, neither},
    {"use.hand", "use.hand result=0", UseReport, neither},
    {"use.hold", "use.hold result=0", UseReport, neither},
    {"use.filter", "use.filter result=0", UseReport, neither},
    {"use.trace", "use.trace result=0", UseReport, neither},
    {"use.back", "use.back result=0", UseReport, neither},
    {"ik.widesupport", "ik.widesupport result=0 wide=1", nullptr, neither},
    {"ik.supportlatch", "ik.supportlatch result=0 latch=1", nullptr, neither},
    {"ik.longreach", "ik.longreach result=0", AnimIkLongReachReport, neither},
    {"ik.skel", "ik.skel result=1", nullptr, neither},
    {"ik.helpers", "ik.helpers result=1 (see PreyVR.log: preyvr_anim_ik helper)", nullptr, neither},
    {"pose.mode", "pose.mode result=0", HandPoseReport, neither},
    {"pose.curl", "pose.curl result=0", HandPoseReport, neither},
    {"pose.curl 80", "pose.curl result=0", HandPoseReport, neither},
    {"pose.curl abc", "pose.curl result=0", HandPoseReport, neither},   // the default, 100
    {"pose.grip 1 2", "pose.grip result=0", HandPoseReport, neither},   // needs 4 values: nothing set
    {"pose.reach 1", "pose.reach result=0", HandPoseReport, neither},   // needs 2
    {"near.follow", "near.follow result=0 followedDeci=0", nullptr, neither},
    {"pose.roll", "pose.roll result=0", HandPoseReport, neither},
    {"pose.stretch", "pose.stretch result=0", HandPoseReport, neither},
    {"arm.report", "arm.report result=0", ArmReport, neither},
    {"body.report", "body.report result=0", BodyFollowReport, neither},
    {"body.follow", "body.follow result=0", BodyFollowReport, neither},
    {"body.level", "body.level result=0", BodyFollowReport, neither},
    {"body.level 2", "body.level result=87", BodyFollowReport, neither},
    {"body.torso 1", "body.torso result=0", BodyFollowReport, neither},   // needs 2
    {"body.turn", "body.turn result=87", BodyFollowReport, neither},      // 0 degrees
    {"body.turn 30", "body.turn result=21", BodyFollowReport, neither},   // not in gameplay
    {"body.roomscale", "body.roomscale result=0", BodyFollowReport, neither},
    {"body.zone 1", "body.zone result=0", BodyFollowReport, neither},     // needs 2
    {"body.radius", "body.radius result=0", BodyFollowReport, neither},
    {"body.radius 200", "body.radius result=0", BodyFollowReport, neither},
    {"body.radius 99", "body.radius result=87", BodyFollowReport, neither},
    {"body.radius abc", "body.radius result=0", BodyFollowReport, neither},   // the default, 350
    {"body.physics", "body.physics result=0", BodyFollowReport, neither},
    {"body.standaside", "body.standaside result=0", BodyFollowReport, neither},
    {"body.nudge", "body.nudge result=87", BodyFollowReport, neither},
    {"arm.lift", "arm.lift result=0", ArmReport, neither},
    {"arm.bogus 1", "arm.bogus result=1", ArmReport, neither},
    {"pose.report", "pose.report result=0", HandPoseReport, neither},
    {"pose.marks", "pose.marks result=0\n", HandPoseMarks, neither},
    {"dbg.draw", "dbg.draw result=0", DebugOverlayReport, neither},
    {"dbg.draw bogus", "dbg.draw result=87", DebugOverlayReport, neither},
    {"dbg.draw 99999", "dbg.draw result=87", DebugOverlayReport, neither},
    {"dbg.draw on", "dbg.draw result=0", DebugOverlayReport, neither},
    {"dbg.draw off", "dbg.draw result=0", DebugOverlayReport, neither},
    {"dbg.lasers", "dbg.lasers result=0", DebugOverlayReport, neither},
    {"move.firehold", "move.firehold result=0 hold=1 posted=0", nullptr, neither},
    {"move.firehold 0", "move.firehold result=0 hold=0 posted=0", nullptr, neither},
    {"move.firehold 1", "move.firehold result=0 hold=1 posted=0", nullptr, neither},
    {"aim.shotprobe", "aim.shotprobe result=0", ShotProbeReport, neither},
    {"aim.shot", "aim.shot result=0", ShotRayReport, neither},
    {"aim.shot 2", "aim.shot result=87", ShotRayReport, neither},
    {"aim.shot abc", "aim.shot result=87", ShotRayReport, neither},   // refused, not defaulted
    {"near.fx", "near.fx result=0 ", NearParticleStereoReport, neither},
    {"near.fx 2", "near.fx result=87 ", NearParticleStereoReport, neither},
    {"near.fxbridge", "near.fxbridge result=0 ", NearParticleStereoReport, neither},
    {"markers.depth", "markers.depth result=0 ", MarkerDepthReport, neither},
    {"markers.depth 2", "markers.depth result=87 ", MarkerDepthReport, neither},
    {"markers.depth abc", "markers.depth result=87 ", MarkerDepthReport, neither},   // refused
    {"markers.size", "markers.size result=0 ", MarkerDepthReport, neither},
    {"markers.size 150", "markers.size result=0 ", MarkerDepthReport, neither},
    {"markers.size 10", "markers.size result=87 ", MarkerDepthReport, neither},
    {"markers.size abc", "markers.size result=0 ", MarkerDepthReport, neither},   // the default, 100
    {"near.fxhide", "near.fxhide result=0 note=no_value_nothing_changed", nullptr, neither},
    {"dbg.report", "dbg.report result=0", DebugOverlayReport, neither},
    {"dbg.marks", "dbg.marks result=0\n", DebugOverlayMarks, neither},

    // The lane dispatchers. An unknown verb under their prefixes is answered,
    // not rejected; wrist values are atoi'd, so a malformed one is 0.
    {"carry.bogus", "carry.bogus result=1", nullptr, neither},
    {"grenade.bogus", "grenade.bogus result=1", nullptr, neither},
    {"mem.peek", "mem.peek result=87", nullptr, neither},
    {"mem.str", "mem.str result=87", nullptr, neither},
    {"mem.scan", "mem.scan result=87", nullptr, neither},
    {"mem.deep", "mem.deep result=87", nullptr, neither},
    {"mem.watch 9 x", "mem.watch result=87", nullptr, neither},
    {"mem.stack", "mem.stack result=2", nullptr, neither},
    {"mem.hits", "mem.hits result=0 count=0 slots=0,0,0,0", nullptr, neither},
    {"arms.att", "arms.att result=2", nullptr, neither},
    {"arms.hide", "arms.hide result=2", nullptr, neither},
    {"arms.draw", "arms.draw result=0 (requested)", nullptr, neither},
    {"arms.free", "arms.free result=0", ArmsReport, neither},
    {"arms.report", "arms.report result=0 player=0x0 ikOwner=0x0 rig=0 weapon=0x0 character=0x0 attachment=0x0",
     ArmsReport, neither},
    {"arms.bogus", "arms.bogus result=1 detail=unknown_verb", nullptr, neither},
    {"wrist.report", "wrist.report result=0", WristReport, neither},
    {"wrist.anchor", "wrist.anchor result=0 anchor=1", nullptr, neither},
    {"wrist.anchor 0", "wrist.anchor result=0 anchor=0", nullptr, neither},
    {"wrist.anchor abc", "wrist.anchor result=0 anchor=0", nullptr, neither},
    {"wrist.anchor 1", "wrist.anchor result=0 anchor=1", nullptr, neither},
    {"wrist.occlusion", "wrist.occlusion result=0 mask=3", nullptr, neither},
    {"wrist.occlusion 9", "wrist.occlusion result=0 mask=3", nullptr, neither},   // clamped
    {"wrist.force", "wrist.force result=0 force=0", nullptr, neither},
    {"wrist.face", "wrist.face result=0 face=1", nullptr, neither},
    {"wrist.haptic", "wrist.haptic result=0 haptic=1", nullptr, neither},
    {"wrist.native", "wrist.native result=0 scale=400 background=0", nullptr, neither},
    {"wrist.place", "wrist.place result=0", WristReport, neither},   // no bare form: re-stores, clamped
    {"wrist.gate", "wrist.gate result=0", WristReport, neither},
    {"wrist.bogus", "wrist.bogus result=1 detail=unknown_verb", nullptr, neither},

    // ExecuteDualCommand
    {"dual.report", "dual.report result=0", DualReports, neither},
    {"dual.observe", "dual.observe result=0", DualSceneReport, neither},
    {"dual.mode", "dual.mode result=0", DualSceneReport, neither},
    {"dual.ipd", "dual.ipd result=0", DualSceneReport, neither},
    {"dual.vr", "dual.vr result=0", DualSceneReport, neither},
    {"dual.overlay", "dual.overlay result=0", DualOverlayReport, neither},
    {"cull.report", "cull.report result=0", CullCameraWatchReport, neither},
    {"cull.watch", "cull.watch result=0", CullCameraWatchReport, neither},
    {"cull.probe", "cull.probe result=0", FrameProbeReport, neither},
    {"cull.prepare", "cull.prepare result=0", CullPrepareReport, neither},
    {"window.hold", "window.hold result=0 note=no_value_nothing_changed", FocusHoldReport, neither},
    {"window.hold 2", "window.hold result=1", FocusHoldReport, neither},
    {"window.hold 0", "window.hold result=0", FocusHoldReport, neither},
    {"xr.hold", "xr.hold result=0 note=no_value_nothing_changed", XrHoldReport, neither},
    {"xr.hold 1 5", "xr.hold result=87", XrHoldReport, neither},
    {"xr.hold 2", "xr.hold result=87", XrHoldReport, neither},

    // ExecuteReticle, ExecutePrompts. A malformed ui.prompts value is 1: on.
    {"aim.world", "aim.world result=0", WorldReticleReports, neither},
    {"aim.world 2", "aim.world result=1", WorldReticleReports, neither},
    {"aim.reticlesize", "aim.reticlesize result=0", ReticleWorldReport, neither},
    {"aim.reticlesize 150", "aim.reticlesize result=0", ReticleWorldReport, neither},
    {"aim.reticlesize 10", "aim.reticlesize result=1", ReticleWorldReport, neither},
    {"aim.reticlesize abc", "aim.reticlesize result=0", ReticleWorldReport, neither},   // the default, 100
    {"ui.prompts", "ui.prompts result=0 enabled=0", ControllerPromptsReport, neither},
    {"ui.prompts 2", "ui.prompts result=1 enabled=0", ControllerPromptsReport, neither},
    {"ui.prompts abc", "ui.prompts result=0 enabled=1", ControllerPromptsReport, neither},
    {"ui.prompts 0", "ui.prompts result=0 enabled=0", ControllerPromptsReport, neither},

    // ExecuteContact, ExecuteSurface, ExecuteSwing, ExecuteWrenchDraw: no result=.
    {"contact", "contact", ContactReport, neither},
    {"contact log", "contact", ContactReport, neither},
    {"contact log 0", "contact", ContactReport, neither},
    {"surface", "surface", SurfaceStopReport, neither},
    {"surface 2", "surface", SurfaceStopReport, neither},             // not 0 or 1: ignored
    {"surface log 1", "surface", SurfaceStopReport, neither},
    {"surface log 0", "surface", SurfaceStopReport, neither},
    {"surface heavy abc", "surface", SurfaceStopReport, neither},     // 1 kg, the minimum
    {"surface heavy 10", "surface", SurfaceStopReport, neither},
    {"swing", "swing", SwingReport, neither},
    {"swing log 1", "swing", SwingReport, neither},
    {"swing log 0", "swing", SwingReport, neither},
    {"wrench.draw", "wrench.draw result=0", WrenchDrawReport, neither},
    {"wrench.draw abc", "wrench.draw result=0", WrenchDrawReport, neither},   // 1: on, the default
    {"wrench.sound", "wrench.sound result=0", WrenchSoundReport, neither},
    {"wrench.sound play", "wrench.sound result=0", WrenchSoundReport, neither},
    {"wrench.sound play 0", "wrench.sound result=0", WrenchSoundReport, neither},
    {"wrench.sound play abc", "wrench.sound result=0", WrenchSoundReport, neither},

    // Execute's own chain: processed. A bare setter only reports; a malformed
    // value is the verb's default (A9) unless the verb refuses it.
    {"vr.options", "vr.options", VrOptionsReports, processed},
    {"vr.options 0", "vr.options", VrOptionsReports, processed},
    {"vr.status", "", VrModeReport, processed},
    {"vr.recenter", "vr.recenter result=1", nullptr, processed},
    {"hud.var", "hud.var result=0 ", HudVariableReport, processed},
    {"hud.layer", "hud.layer result=0 ", HudLayerReport, processed},
    {"input.hotkeys", "input.hotkeys result=0 consoleToggles=0", nullptr, processed},
    {"ui.stereo", "ui.stereo result=0 ", InventoryStereoReport, processed},
    {"ui.stereo 2", "ui.stereo result=1 ", InventoryStereoReport, processed},
    {"ui.depth", "ui.depth result=0 ", InventoryStereoReport, processed},
    {"ui.depth 50", "ui.depth result=0 ", InventoryStereoReport, processed},
    {"ui.depth 101", "ui.depth result=1 ", InventoryStereoReport, processed},
    {"ui.depth abc", "ui.depth result=0 ", InventoryStereoReport, processed},
    {"ui.inventory", "ui.inventory result=0 ", InventoryLayerReports, processed},
    {"ui.panel", "ui.panel result=0 mode=0 ", VrModeReport, processed},
    {"ui.panel 3", "ui.panel result=1 mode=0 ", VrModeReport, processed},
    {"ui.pointer", "ui.pointer result=0 ", UiPointerReport, processed},
    {"ui.pointer 3", "ui.pointer result=87 ", UiPointerReport, processed},
    {"ui.scale", "ui.scale result=0 percent=100 margin=72 guide=0", nullptr, processed},
    {"ui.scale 150", "ui.scale result=0 percent=150 margin=72 guide=0", nullptr, processed},
    {"ui.scale 10", "ui.scale result=1 percent=150 margin=72 guide=0", nullptr, processed},
    {"ui.scale abc", "ui.scale result=0 percent=100 margin=72 guide=0", nullptr, processed},
    {"ui.text", "ui.text result=0 cm=140", nullptr, processed},
    {"ui.text 150", "ui.text result=0 cm=150", nullptr, processed},
    {"ui.text 50", "ui.text result=1 cm=150", nullptr, processed},
    {"ui.text -1", "ui.text result=1 cm=150", nullptr, processed},     // wraps unsigned: out of range
    {"ui.text abc", "ui.text result=0 cm=140", nullptr, processed},
    {"ui.text 0x96", "ui.text result=0 cm=150", nullptr, processed},   // integers parse with base 0
    {"ui.text 12abc", "ui.text result=0 cm=140", nullptr, processed},
    {"  ui.text\t\t150 ", "ui.text result=0 cm=150", nullptr, processed},
    {"ui.text 140", "ui.text result=0 cm=140", nullptr, processed},
    {"ui.distance", "ui.distance result=0 cm=200", nullptr, processed},
    {"ui.distance 0310", "ui.distance result=0 cm=200", nullptr, processed},   // octal 310
    {"ui.distance abc", "ui.distance result=0 cm=200", nullptr, processed},
    {"ui.offset", "ui.offset result=0 right=0 up=0", nullptr, processed},
    {"ui.offset 10 5", "ui.offset result=0 right=10 up=5", nullptr, processed},
    {"ui.offset 10", "ui.offset result=87 right=10 up=5", nullptr, processed},   // both or neither
    {"ui.offset x 5", "ui.offset result=87 right=10 up=5", nullptr, processed},
    {"ui.offset 100 0", "ui.offset result=1 right=10 up=5", nullptr, processed},
    {"ui.offset 0 0", "ui.offset result=0 right=0 up=0", nullptr, processed},
    {"ui.margin", "ui.margin result=0 percent=72", nullptr, processed},
    {"ui.margin 80", "ui.margin result=0 percent=80", nullptr, processed},
    {"ui.margin 10", "ui.margin result=1 percent=80", nullptr, processed},
    {"ui.margin abc", "ui.margin result=0 percent=72", nullptr, processed},
    {"ui.guide", "ui.guide result=0 enabled=0", nullptr, processed},
    {"ui.guide 1", "ui.guide result=0 enabled=1", nullptr, processed},
    {"ui.guide abc", "ui.guide result=0 enabled=0", nullptr, processed},
    {"ui.curve", "ui.curve result=0 degrees=35", nullptr, processed},
    {"ui.curve 40", "ui.curve result=0 degrees=40", nullptr, processed},
    {"ui.curve 61", "ui.curve result=87 degrees=40", nullptr, processed},
    {"ui.curve abc", "ui.curve result=0 degrees=35", nullptr, processed},
    {"observer", "observer status=unavailable(0)", nullptr, processed},
    {"xr.srgb", "xr.srgb result=0 note=no_value_nothing_changed", nullptr, processed},
    {"xr.native", "xr.native result=0 note=no_value_nothing_changed", nullptr, processed},
    {"xr.stereo", "xr.stereo result=0 note=no_value_nothing_changed", nullptr, processed},
    {"xr.mirrorfov", "xr.mirrorfov result=0 percent=100", nullptr, processed},
    {"xr.mirrorfov 150", "xr.mirrorfov result=0 percent=150", nullptr, processed},
    {"xr.mirrorfov 10", "xr.mirrorfov result=0 percent=50", nullptr, processed},   // clamped
    {"xr.mirrorfov abc", "xr.mirrorfov result=0 percent=100", nullptr, processed},
    {"xr.submit", "xr.submit result=0 note=no_value_nothing_changed", nullptr, processed},
    {"view.observe", "view.observe result=0 note=no_value_nothing_changed", nullptr, processed},
    {"view.calibrate", "view.calibrate result=1 ", HeadHeightReport, processed},
    {"view.height", "view.height ", HeadHeightReport, processed},
    {"view.recenter", "view.recenter result=1", nullptr, processed},
    {"view.apply", "view.apply result=0 note=no_value_nothing_changed", nullptr, processed},
    {"view.position", "view.position result=0 note=no_value_nothing_changed", nullptr, processed},
    {"near.fov", "near.fov result=0 deciDegrees=0 applied=0 refused=0 observedDeciDegrees=0 observedValid=0"
                 " faulted=0 thread=0", nullptr, processed},
    // A value, even a refused one, first turns near.follow off.
    {"near.fov abc", "near.fov result=1 deciDegrees=0 applied=0 refused=0 observedDeciDegrees=0 observedValid=0"
                     " faulted=0 thread=0", nullptr, processed},
    {"near.fov -5", "near.fov result=1 deciDegrees=0 applied=0 refused=0 observedDeciDegrees=0 observedValid=0"
                    " faulted=0 thread=0", nullptr, processed},
    {"near.fov 1 2", "near.fov result=1 deciDegrees=0 applied=0 refused=0 observedDeciDegrees=0 observedValid=0"
                     " faulted=0 thread=0", nullptr, processed},
    {"near.follow 1", "near.follow result=0 followedDeci=0", nullptr, neither},
    {"near.enable", "near.enable result=0 note=no_value_nothing_changed", nullptr, processed},
    {"near.halfipd", "near.halfipd result=0 note=no_value_nothing_changed", nullptr, processed},
    {"near.lineage", "near.lineage result=0 note=no_value_nothing_changed", nullptr, processed},
    {"near.dump", "near.dump result=1", nullptr, processed},
    {"near.zero", "near.zero result=0 note=no_value_nothing_changed", nullptr, processed},
    {"ik.mode", "ik.mode result=0 mode=0(off)", nullptr, processed},
    {"ik.test", "ik.test result=0 note=no_value_nothing_changed", nullptr, processed},
    {"ik.drive", "ik.drive result=0 note=no_value_nothing_changed", nullptr, processed},
    {"ik.calibrate", "ik.calibrate result=1", nullptr, processed},
    {"ik.align", "ik.align result=0", AnimIkWeaponAlignmentReport, processed},
    {"ik.align abc", "ik.align result=1", AnimIkWeaponAlignmentReport, processed},
    {"ik.align 1 2", "ik.align result=1", AnimIkWeaponAlignmentReport, processed},
    {"ik.joints", "ik.joints result=0 note=no_value_nothing_changed", nullptr, processed},
    {"ik.reach", "ik.reach result=0 percent=100", nullptr, processed},
    {"ik.reach 120", "ik.reach result=0 percent=120", nullptr, processed},
    {"ik.reach 10", "ik.reach result=1 percent=120", nullptr, processed},
    {"ik.reach abc", "ik.reach result=0 percent=100", nullptr, processed},
    {"ik.hands", "ik.hands result=0 note=no_value_nothing_changed", nullptr, processed},
    {"ik.anchor", "ik.anchor result=0 note=no_value_nothing_changed camera=0 reticle=0 missing=0", nullptr,
     processed},
    {"ik.trace", "ik.trace result=0", AnimIkTraceReport, processed},
    {"ik.dump", "ik.dump result=1", nullptr, processed},
    // The console allowlist decides; only the last line is queued, and nothing
    // after it may queue, since a full queue is retried for a second.
    {"console exec autoexec.cfg", "console result=1 command=\"exec autoexec.cfg\"", nullptr, processed},
    {"console hud_bobHud abc", "console result=1 command=\"hud_bobHud abc\"", nullptr, processed},
    {"hud_bobHud abc", "console result=1 command=\"hud_bobHud abc\"", nullptr, processed},
    {"HUD_BOBHUD 1 2", "console result=1 command=\"HUD_BOBHUD 1 2\"", nullptr, processed},
    {"console hud_bobHud 0", "console result=0 command=\"hud_bobHud 0\"", nullptr, processed},
    {"psi.target", "psi.target result=0", PsiMedkitReport, processed},
    {"medkit.slot", "medkit.slot result=0", PsiMedkitReport, processed},
    {"aim.scene", "aim.scene", SceneQueryReport, processed},
    {"haptics", "haptics", HapticsReport, processed},
    {"aim.reticle", "aim.reticle result=0 note=no_value_nothing_changed", nullptr, processed},
    {"aim.reticleconverge", "aim.reticleconverge result=0 mm=10000", nullptr, processed},
    {"aim.reticleconverge 1000", "aim.reticleconverge result=0 mm=1000", nullptr, processed},
    {"aim.reticleconverge 10", "aim.reticleconverge result=0 mm=500", nullptr, processed},   // clamped
    {"aim.reticleconverge abc", "aim.reticleconverge result=0 mm=10000", nullptr, processed},
    {"aim.reticledispatch", "aim.reticledispatch result=0 note=no_value_nothing_changed", nullptr, processed},
    {"aim.reticlecanvas", "aim.reticlecanvas result=0 mode=1 native=0 nativeFailed=0", nullptr, processed},
    {"aim.reticlestage", "aim.reticlestage result=0 note=no_value_nothing_changed", nullptr, processed},
    {"aim.twohand", "aim.twohand result=0", TwoHandedAimStatus, processed},
    {"aim.bodyyaw", "aim.bodyyaw result=0 value=1", nullptr, processed},
    {"aim.bodyyaw 2", "aim.bodyyaw result=1 value=1", nullptr, processed},
    {"aim.bodyyaw abc", "aim.bodyyaw result=0 value=1", nullptr, processed},
    {"aim.origin", "aim.origin result=0 mode=0(native)", nullptr, processed},
    {"aim.calibratebarrel", "aim.calibratebarrel result=1 offsetMm=0,0,0", nullptr, processed},
    {"aim.enable", "aim.enable result=0 note=no_value_nothing_changed applied=0 nativeMagMilli=0", nullptr,
     processed},
    {"weapon.observe", "weapon.observe result=0 note=no_value_nothing_changed", nullptr, processed},
    {"hud.call", "hud.call result=rejected detail=need_function_and_value", nullptr, processed},
    {"hud.call fn", "hud.call result=rejected detail=need_function_and_value", nullptr, processed},
    {"xr.frustum", "xr.frustum result=0 note=no_value_nothing_changed used=0 missing=0", XrCoverageReport,
     processed},
    {"xr.timing", "xr.timing result=0", XrTimingReport, processed},
    {"xr.coverage", "xr.coverage result=0", XrCoverageReport, processed},
    {"move.act", "move.act result=0", InteractionReport, processed},
    {"move.bind 99 1", "move.bind result=2", InteractionReport, processed},
    {"menu.gate", "menu.gate result=0 note=no_value_nothing_changed menuOpen=0 samples=0 suppressed=0", nullptr,
     processed},
    {"menu.nav", "menu.nav result=0 note=no_value_nothing_changed actions=0", nullptr, processed},
    {"hud.probe", "hud.probe result=0 ", HudLastProbe, processed},
    {"hud.probe 1", "hud.probe result=0 ", HudLastProbe, processed},
    {"xr.resolution", "xr.resolution result=0 recommended=0x0 max=0x0 backbuffer=0x0 heldEye=0x0 submitted=0x0"
                      " recommendedSamples=0 viewsDiffer=0 heldFormat=0 views=0 sizeMismatch=0", nullptr, processed},
    {"move.mode", "move.mode result=0 mode=0(off)", nullptr, processed},
    {"move.snap", "move.snap result=0 degrees=45", nullptr, processed},
    {"move.snap 30", "move.snap result=0 degrees=30", nullptr, processed},
    {"move.snap abc", "move.snap result=0 degrees=30", nullptr, processed},   // the current value
    {"move.snap 5", "move.snap result=1 degrees=30", nullptr, processed},
    {"move.snap 45", "move.snap result=0 degrees=45", nullptr, processed},
    {"move.headrelative", "move.headrelative result=0 enabled=1", nullptr, processed},
    {"move.headrelative abc", "move.headrelative result=1 enabled=1", nullptr, processed},   // defaults to 2
    {"move.scales", "move.scales result=0 strafe=100 backward=100", nullptr, processed},
    {"move.scales 50", "move.scales result=0 strafe=50 backward=100", nullptr, processed},
    {"move.scales abc abc", "move.scales result=0 strafe=100 backward=100", nullptr, processed},
    {"move.turn", "move.turn result=0 enabled=0", nullptr, processed},
    {"move.turndeadzone", "move.turndeadzone result=0 hundredths=15", nullptr, processed},
    {"move.turndeadzone 61", "move.turndeadzone result=1 hundredths=15", nullptr, processed},
    {"move.turndeadzone abc", "move.turndeadzone result=0 hundredths=15", nullptr, processed},
    {"move.turnscale", "move.turnscale result=0 note=no_value_nothing_changed", nullptr, processed},
    {"move.fire", "move.fire result=0 enabled=0", nullptr, processed},
    {"move.all", "move.all result=0 note=no_value_nothing_changed", InteractionReport, processed},
    {"move.all 0", "move.all result=0 move=0 turn=0 fire=0 act=0", InteractionReport, processed},
    {"move.deadzone", "move.deadzone result=0 hundredths=15", nullptr, processed},
    {"move.deadzone 61", "move.deadzone result=1 hundredths=15", nullptr, processed},
    {"move.deadzone abc", "move.deadzone result=0 hundredths=15", nullptr, processed},
    {"input.post", "input.post result=0 note=no_value_nothing_changed", nullptr, processed},
    {"input.force", "input.force result=0 note=no_value_nothing_changed", nullptr, processed},
    {"input.binds", "input.binds result=0 (next frame; see PreyVR.log: preyvr_bindings)", nullptr, processed},
};
// Not run here, each for what its handler does with no Prey around it:
// - start a session or the VR mode: vr.enable, vr.disable, xr.start, xr.submit
//   with a value; xr.runtime <path> sets XR_RUNTIME_JSON for the process.
// - start a thread: input.hotkeys with a value.
// - install hooks, given a value (the bare forms above only report, and values
//   refused before the setter runs are pinned): observer, near.fov, near.enable,
//   near.fxhide, aim.shotprobe, aim.reticle, aim.enable, weapon.observe,
//   view.observe, ik.mode, move.mode, use.hand, xr.stereo, ui.inventory,
//   hud.layer, psi.target, medkit.slot, dual.observe, dual.mode, dual.overlay,
//   dual.vr, cull.watch, cull.probe, cull.prepare; aim.shot, near.fx,
//   markers.depth, ui.stereo and window.hold given 1; aim.beam and dual.capture
//   even bare; hud.var, hud.setvar, hud.call, hud.fit and hud.probe given their
//   arguments.
// - post input or arm hardware breakpoints: menu, input.key, mem.watch with an
//   address.
// - call Prey through an unset base (gBase is 0 until the lane installs): every
//   known carry.* and grenade.* verb, since each ends in its lane's report
//   (grenade.give also installs the lane).
// - read or write raw memory: mem.peek, mem.str, mem.scan and mem.deep with an
//   address, arms.joints, arms.hide with an index.
// - capture frames: capture, capture.hud, capture.inventory.
// - wait: use.find (half a second), a second allowlisted console command
//   (a second of retries while the queue is full).

// The report's own fields, in order, and the reports it embeds where they fall:
// names, not values, apart from the channel's counters.
std::vector<std::string> ReportSkeleton(unsigned long long processedCount, unsigned long long rejectedCount)
{
    std::vector<std::string> s{"smoke=n/a focus={" + FocusHoldReport() + " }"};
    const auto names = [&s](std::initializer_list<const char*> list) {
        for (const char* name : list) { s.push_back(std::string(" ") + name + "="); }
    };
    names({"observerFrames", "xrSession", "xrFrames", "fovAgreed", "fovDiverged", "projectionPolicy", "cameraEdit",
           "lastEye", "viewObserved", "viewApplied", "viewPoseFallback", "posApplied", "posRefused", "posOffsetMm",
           "nearApplied", "nearRefused", "nearNoEye", "nearReentered", "nearAlreadyOffset", "nearNoProvenance",
           "eyeLookupMiss", "nearDeltaUm"});
    s.push_back(" " + NearParticleStereoReport());
    names({"ikMode", "ikHooked", "ikOwner", "ikEquipGen", "ikPoseSeq", "ikNoOwner", "ikBusy", "ikCalls",
           "ikMatched", "ikRig", "ikJoints", "ikGate", "ikCvar", "ikTargetR", "ikWeightR", "ikLimbEndR",
           "ikLimbTagR", "ikTargetL", "ikWeightL", "ikLimbEndL", "ikWrittenR", "ikWrittenL", "ikNoPose",
           "ikClamped", "ikReach", "ikCalR", "ikCalL", "ikLocMm", "ikLocYawMdeg", "ikGoalMm", "weaponAttachment",
           "weaponBone", "weaponSim"});
    s.push_back(WeaponMuzzleAlignmentReport() + ShotRayReport());
    names({"aimOriginApplied", "aimSamples", "aimOriginMode", "aimMuzzleApplied", "aimMuzzleUnavailable",
           "barrelCalibrations", "barrelOffsetMm", "reticleApplied", "reticleOffScreen", "reticleXY",
           "reticleCanvasXY", "reticleCanvasMode", "reticleNativeCanvas", "reticleNativeCanvasFailed",
           "reticleDispatched", "reticleDispatchFailed"});
    s.push_back(ReticleProjectionReport());
    names({"aimDirMilli", "aimRawQMilli", "recenters", "reticleOriginMm", "reticleConvergeMm", "hudElement",
           "hudCalls", "hudRefused", "captures", "aimBodyYaw", "camYawMdeg", "headYawMdeg", "playYawMdeg", "eyeMm",
           "headMm", "yawHeld", "yawUnavailable", "weaponMountMm", "weaponBaseline", "aimApplied", "aimNoPose",
           "aimNoPlayer", "aimCompose", "aimNativeMagMilli", "menuActions", "inputPosted", "menuDiscarded",
           "moveMode", "moveHooked", "moveOursX", "moveOursY", "moveNative", "movePosted", "moveDropped",
           "moveInputObj", "moveAxisMilli", "moveCinematic", "turnEnabled", "turnPosted", "turnRefused"});
    s.push_back(SnapTurnReport());
    names({"fireEnabled", "firePressed", "fireReleased", "fireHold", "fireHoldPosted", "fireRefused",
           "mirrorFovPct", "xrSyncs", "xrNotFocused", "xrLocatedL", "xrLocatedR"});
    s.push_back(" stickL=unavailable stickR=unavailable");   // no tracking frame
    names({"inputDropped", "inputDrainThread"});
    s.push_back(" channelProcessed=" + std::to_string(processedCount) +
                " channelRejected=" + std::to_string(rejectedCount));
    return s;
}

unsigned ThreadCount()
{
    unsigned n = 0;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        n += entry.th32OwnerProcessID == GetCurrentProcessId() ? 1u : 0u;
    }
    CloseHandle(snapshot);
    return n;
}

void TestTable()
{
    static const char* const kCounted[] = {"processed", "rejected", "neither"};
    unsigned failures = 0;
    for (const Case& c : kCases) {
        const auto processedBefore = gProcessed.load(), rejectedBefore = gRejected.load();
        const std::string reply = Run(c.line);
        const std::string want = c.reply + (c.tail ? c.tail() : std::string());
        const auto p = gProcessed.load() - processedBefore, r = gRejected.load() - rejectedBefore;
        const int counted = p == 1 && r == 0   ? processed
                            : p == 0 && r == 1 ? rejected
                            : p == 0 && r == 0 ? neither
                                               : -1;
        if (reply != want || counted != c.counted) {
            ++failures;
            std::cerr << "MISMATCH \"" << c.line << "\" (" << kCounted[c.counted] << ", counted "
                      << (counted < 0 ? "twice" : kCounted[counted]) << ")\n  want: " << want << "\n  got:  "
                      << reply << '\n';
        }
    }
    Require(failures == 0, "every command line's reply and count are as pinned");
}

void TestReport()
{
    gLines.clear();
    const auto processedBefore = gProcessed.load(), rejectedBefore = gRejected.load();
    const std::string reply = Run("report");
    Require(gProcessed.load() == processedBefore + 1 && gRejected.load() == rejectedBefore, "report is processed");
    std::size_t at = 0;
    for (const std::string& part : ReportSkeleton(processedBefore, rejectedBefore)) {
        const std::size_t found = reply.find(part, at);
        if (found == std::string::npos || (at == 0 && found != 0)) {
            std::cerr << "report is missing, or has out of order: \"" << part << "\"\n";
            Require(false, "the report's fields, in order");
        }
        at = found + part.size();
    }
    Require(at == reply.size(), "the report ends with the channel's counters");
    Require(gLines.size() == 1 && gLines.back() == "preyvr_report " + reply, "and is logged as it is replied");

    // The log takes the whole reply stream so far, as the command file step builds one
    // for every line of a command file -- not only the report.
    std::ostringstream batch;
    batch << "ui.text result=0 cm=140\n";
    Execute(Split("report"), batch);
    Require(gLines.size() == 2 && gLines.back() == "preyvr_report " + batch.str(),
            "report logs the batch's earlier replies with its own");
}

void TestLoggedReplies()
{
    // results.txt is replaced by the next command, so these log their report.
    gLines.clear();
    Require(Run("ik.trace") == "ik.trace result=0" + AnimIkTraceReport() && gLines.size() == 1 &&
                gLines.back() == "preyvr_ik_trace" + AnimIkTraceReport(),
            "ik.trace logs its trace");
    gLines.clear();
    Require(Run("xr.timing") == "xr.timing result=0" + XrTimingReport() && gLines.size() == 1 &&
                gLines.back() == "preyvr_timing" + XrTimingReport(),
            "xr.timing logs its timing");
}

// The worker thread's two calls into the channel. The start line is the one
// Start-PreyVR.ps1 waits for before it writes its startup commands; the command
// file is read whole, truncated before anything runs, and answered one line per
// command, with comments and blank lines skipped.
void TestCommandFile()
{
    gLines.clear();
    LogCommandChannelStarted();
    Require(gLines.size() == 1 && gLines.back().rfind("preyvr_channel result=0 detail=started commands=\"", 0) == 0,
            "the worker's start line is the one the launcher waits for");

    const auto commands = CommandPath();
    const auto results = ResultPath();
    std::error_code error;
    std::filesystem::create_directories(commands.parent_path(), error);
    std::filesystem::remove(results, error);
    std::ofstream(commands, std::ios::trunc) << "# a comment\n\n  bogus\nbogus two\n";
    const auto rejectedBefore = gRejected.load();
    ServiceCommandFile();
    Require(std::filesystem::file_size(commands, error) == 0, "the command file is truncated");
    std::ostringstream text;
    text << std::ifstream(results).rdbuf();
    const std::string reply = "result=rejected verb=\"bogus\" detail=not_a_channel_verb_and_not_allowlisted\n";
    Require(text.str() == reply + reply, "one reply per command, comments and blank lines skipped");
    Require(gRejected.load() == rejectedBefore + 2, "and each is counted");

    // An empty file runs nothing and leaves the last results in place.
    ServiceCommandFile();
    std::ostringstream again;
    again << std::ifstream(results).rdbuf();
    Require(again.str() == reply + reply && gRejected.load() == rejectedBefore + 2, "an empty command file is left alone");
    std::filesystem::remove(results, error);
}

} // namespace

int main()
{
    Require(GetModuleHandleW(L"PreyDll.dll") == nullptr, "no Prey in this process");
    Require(GetModuleHandleW(L"openxr_loader.dll") == nullptr, "and no OpenXR loader");
    const unsigned threads = ThreadCount();

    TestTable();
    TestReport();
    TestLoggedReplies();
    TestCommandFile();

    Require(GetModuleHandleW(L"openxr_loader.dll") == nullptr, "no command loaded the OpenXR loader");
    Require(preyvr::test::gMinHookCalls == 0, "no command tried to install a hook");
    Require(ThreadCount() <= threads, "no command started a thread");
    std::cout << "command channel: " << std::size(kCases) << " command lines pinned\n";
    return 0;
}
