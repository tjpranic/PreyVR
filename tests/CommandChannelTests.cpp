// The command channel's replies, pinned before its verb chains become a table:
// for each command line below, the exact reply and how the channel counts it --
// processed or rejected. Every line counts exactly once (decision 1, 10-24): a
// line that counts as neither, or twice, is a mismatch.
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
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
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
std::string TaaReports() { return TaaProbeReport() + " " + TaaLaneReport(); }
std::string WorldReticleReports() { return ReticleWorldReport() + " scene:" + SceneQueryReport(); }
std::string VrOptionsReports() { return VrOptionsReport() + PsychoscopeReport() + BodyEquipmentReport(); }
std::string BodyReports() { return TorsoLaneReport() + BodyPinReport() + RoomScaleReport(); }
std::string BodyDepthReports() { return BodyDepthReport() + " " + ViewNearClipReport(); }
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
    // Not a verb, an unknown one under a lane's prefix, or a verb short of the
    // words it needs: rejected, and counted as rejected. Verbs are
    // case-sensitive; allowlisted cvars are not.
    {"bogus", "result=rejected verb=\"bogus\" detail=not_a_channel_verb_and_not_allowlisted", nullptr, rejected},
    {"VR.STATUS", "result=rejected verb=\"VR.STATUS\" detail=not_a_channel_verb_and_not_allowlisted", nullptr,
     rejected},
    {"vr.status2", "result=rejected verb=\"vr.status2\" detail=not_a_channel_verb_and_not_allowlisted", nullptr,
     rejected},
    {"use.find", "result=rejected verb=\"use.find\" detail=missing_arguments", nullptr, rejected},
    {"hud.setvar path", "result=rejected verb=\"hud.setvar\" detail=missing_arguments", nullptr,
     rejected},
    {"xr.runtime", "result=rejected verb=\"xr.runtime\" detail=missing_arguments", nullptr,
     rejected},
    {"console", "result=rejected verb=\"console\" detail=missing_arguments", nullptr, rejected},
    {"move.bind 1", "result=rejected verb=\"move.bind\" detail=missing_arguments", nullptr,
     rejected},
    {"hud.fit", "result=rejected verb=\"hud.fit\" detail=missing_arguments", nullptr, rejected},
    {"hud.call", "result=rejected verb=\"hud.call\" detail=missing_arguments", nullptr, rejected},
    {"hud.call fn", "result=rejected verb=\"hud.call\" detail=missing_arguments", nullptr, rejected},
    {"carry.bogus", "result=rejected verb=\"carry.bogus\" detail=not_a_channel_verb_and_not_allowlisted", nullptr,
     rejected},
    {"grenade.bogus", "result=rejected verb=\"grenade.bogus\" detail=not_a_channel_verb_and_not_allowlisted", nullptr,
     rejected},
    {"mem.bogus", "result=rejected verb=\"mem.bogus\" detail=not_a_channel_verb_and_not_allowlisted", nullptr,
     rejected},
    {"arms.bogus", "result=rejected verb=\"arms.bogus\" detail=not_a_channel_verb_and_not_allowlisted", nullptr,
     rejected},
    {"wrist.bogus", "result=rejected verb=\"wrist.bogus\" detail=not_a_channel_verb_and_not_allowlisted", nullptr,
     rejected},
    {"arm.bogus 1", "result=rejected verb=\"arm.bogus\" detail=not_a_channel_verb_and_not_allowlisted", nullptr,
     rejected},

    // The hand-pose rows: use, IK, pose, body, arm, the debug overlay, shots,
    // near particles and markers. A refused value is still processed.
    {"use.report", "use.report result=0", UseReport, processed},
    {"use.hand", "use.hand result=0", UseReport, processed},
    {"use.hold", "use.hold result=0", UseReport, processed},
    {"use.filter", "use.filter result=0", UseReport, processed},
    {"use.trace", "use.trace result=0", UseReport, processed},
    {"use.back", "use.back result=0", UseReport, processed},
    {"ik.widesupport", "ik.widesupport result=0 wide=1", nullptr, processed},
    {"ik.supportlatch", "ik.supportlatch result=0 latch=1", nullptr, processed},
    {"ik.longreach", "ik.longreach result=0", AnimIkLongReachReport, processed},
    {"ik.skel", "ik.skel result=1 label=manual", nullptr, processed},
    {"ik.skel stand", "ik.skel result=1 label=stand", nullptr, processed},
    {"ik.skel bad!label", "ik.skel result=87 label=bad!label", nullptr, processed},   // refused before the lane
    {"ik.helpers", "ik.helpers result=1 (see PreyVR.log: preyvr_anim_ik helper)", nullptr, processed},
    {"pose.mode", "pose.mode result=0", HandPoseReport, processed},
    {"pose.curl", "pose.curl result=0", HandPoseReport, processed},
    {"pose.curl 80", "pose.curl result=0", HandPoseReport, processed},
    {"pose.curl abc", "pose.curl result=0", HandPoseReport, processed},   // the default, 100
    {"pose.grip 1 2", "pose.grip result=87", HandPoseReport, processed},   // needs 4 values: refused
    {"pose.reach 1", "pose.reach result=87", HandPoseReport, processed},   // needs 2
    {"near.follow", "near.follow result=0 followedDeci=0", nullptr, processed},
    {"pose.roll", "pose.roll result=0", HandPoseReport, processed},
    {"pose.stretch", "pose.stretch result=0", HandPoseReport, processed},
    {"arm.report", "arm.report result=0", ArmReport, processed},
    // The arms against the drawn torso: a capsule needs its part and all four
    // values; the arm's thickness all four.
    {"arm.capsule", "arm.capsule result=0", ArmReport, processed},
    {"arm.capsule chest 30 -40", "arm.capsule result=87", ArmReport, processed},
    {"arm.capsule chest 30 -40 42 abc", "arm.capsule result=87", ArmReport, processed},
    {"arm.capsule belly 0 0 40 100", "arm.capsule result=1", ArmReport, processed},   // no such part
    {"arm.capsule hips 0 0 40 301", "arm.capsule result=1", ArmReport, processed},    // out of range
    {"arm.capsule hips -15 -40 50 117", "arm.capsule result=0", ArmReport, processed},
    {"arm.limbs 50 45 40", "arm.limbs result=1", ArmReport, processed},
    {"arm.limbs 50 45 40 30", "arm.limbs result=0", ArmReport, processed},
    {"arm.avoid 10 10", "arm.avoid result=1", ArmReport, processed},       // needs all three
    {"arm.avoid 10 0 360", "arm.avoid result=1", ArmReport, processed},    // the wall is at least 1 mm
    {"arm.avoid 10 10 360", "arm.avoid result=0", ArmReport, processed},
    {"body.report", "body.report result=0", BodyReports, processed},
    {"body.torso 1", "body.torso result=87", TorsoLaneReport, processed},   // needs 2
    // The body overhaul's research verbs and test switches are gone, as are the
    // old room-scale's tuning verbs, arm.lift (the spine stretch the body pin
    // replaced) and arm.anchor.
    {"body.follow 0", "result=rejected verb=\"body.follow\" detail=not_a_channel_verb_and_not_allowlisted", nullptr,
     rejected},
    {"body.followafter 0",
     "result=rejected verb=\"body.followafter\" detail=not_a_channel_verb_and_not_allowlisted", nullptr, rejected},
    {"body.roomscale 0", "result=rejected verb=\"body.roomscale\" detail=not_a_channel_verb_and_not_allowlisted",
     nullptr, rejected},
    {"body.pin 0", "result=rejected verb=\"body.pin\" detail=not_a_channel_verb_and_not_allowlisted", nullptr,
     rejected},
    {"body.neck 500 250", "result=rejected verb=\"body.neck\" detail=not_a_channel_verb_and_not_allowlisted", nullptr,
     rejected},
    {"body.pelvis 450", "result=rejected verb=\"body.pelvis\" detail=not_a_channel_verb_and_not_allowlisted", nullptr,
     rejected},
    {"body.throat 80 120", "result=rejected verb=\"body.throat\" detail=not_a_channel_verb_and_not_allowlisted",
     nullptr, rejected},
    {"body.capsule", "result=rejected verb=\"body.capsule\" detail=not_a_channel_verb_and_not_allowlisted", nullptr,
     rejected},
    {"body.posetest sequence",
     "result=rejected verb=\"body.posetest\" detail=not_a_channel_verb_and_not_allowlisted", nullptr, rejected},
    {"body.zone 30 5", "result=rejected verb=\"body.zone\" detail=not_a_channel_verb_and_not_allowlisted", nullptr,
     rejected},
    {"arm.lift", "result=rejected verb=\"arm.lift\" detail=not_a_channel_verb_and_not_allowlisted", nullptr, rejected},
    {"arm.anchor 1", "result=rejected verb=\"arm.anchor\" detail=not_a_channel_verb_and_not_allowlisted", nullptr,
     rejected},
    {"arm.torso abc 600", "arm.torso result=87", ArmReport, processed},   // no default to fall back to
    {"pose.report", "pose.report result=0", HandPoseReport, processed},
    {"pose.marks", "pose.marks result=0\n", HandPoseMarks, processed},
    {"dbg.draw", "dbg.draw result=0", DebugOverlayReport, processed},
    {"dbg.draw bogus", "dbg.draw result=87", DebugOverlayReport, processed},
    {"dbg.draw 99999", "dbg.draw result=87", DebugOverlayReport, processed},
    {"dbg.draw on", "dbg.draw result=0", DebugOverlayReport, processed},
    {"dbg.draw off", "dbg.draw result=0", DebugOverlayReport, processed},
    {"dbg.lasers", "dbg.lasers result=0", DebugOverlayReport, processed},
    {"move.firehold", "move.firehold result=0 hold=1 posted=0", nullptr, processed},
    {"move.firehold 0", "move.firehold result=0 hold=0 posted=0", nullptr, processed},
    {"move.firehold 1", "move.firehold result=0 hold=1 posted=0", nullptr, processed},
    {"aim.shotprobe", "aim.shotprobe result=0", ShotProbeReport, processed},
    {"aim.shot", "aim.shot result=0", ShotRayReport, processed},
    {"aim.shot 2", "aim.shot result=87", ShotRayReport, processed},
    {"aim.shot abc", "aim.shot result=87", ShotRayReport, processed},   // refused, not defaulted
    {"near.fx", "near.fx result=0 ", NearParticleStereoReport, processed},
    {"near.fx 2", "near.fx result=87 ", NearParticleStereoReport, processed},
    {"near.fxbridge", "near.fxbridge result=0 ", NearParticleStereoReport, processed},
    {"markers.depth", "markers.depth result=0 ", MarkerDepthReport, processed},
    {"markers.depth 2", "markers.depth result=87 ", MarkerDepthReport, processed},
    {"markers.depth abc", "markers.depth result=87 ", MarkerDepthReport, processed},   // refused
    {"markers.size", "markers.size result=0 ", MarkerDepthReport, processed},
    {"markers.size 150", "markers.size result=0 ", MarkerDepthReport, processed},
    {"markers.size 10", "markers.size result=87 ", MarkerDepthReport, processed},
    {"markers.size abc", "markers.size result=0 ", MarkerDepthReport, processed},   // the default, 100
    {"stars.depth", "stars.depth result=0 ", StarfieldDepthReport, processed},
    {"stars.depth 2", "stars.depth result=87 ", StarfieldDepthReport, processed},
    {"stars.depth abc", "stars.depth result=87 ", StarfieldDepthReport, processed},   // refused
    {"examine.pullback", "examine.pullback result=0 ", ExamineCameraReport, processed},
    {"examine.pullback 45", "examine.pullback result=0 ", ExamineCameraReport, processed},
    {"examine.pullback 101", "examine.pullback result=87 ", ExamineCameraReport, processed},
    {"examine.pullback abc", "examine.pullback result=87 ", ExamineCameraReport, processed},   // refused
    {"examine.pullback 30", "examine.pullback result=0 ", ExamineCameraReport, processed},
    {"body.depth", "body.depth result=0 ", BodyDepthReports, processed},
    {"body.depth 3", "body.depth result=87 ", BodyDepthReports, processed},
    {"body.depth abc", "body.depth result=87 ", BodyDepthReports, processed},   // refused
    {"near.fxhide", "near.fxhide result=0 note=no_value_nothing_changed", nullptr, processed},
    {"dbg.report", "dbg.report result=0", DebugOverlayReport, processed},
    {"dbg.marks", "dbg.marks result=0\n", DebugOverlayMarks, processed},

    // The lanes' own dispatchers; an unknown verb under their prefixes is
    // rejected (above). A malformed wrist value is the verb's fallback.
    {"mem.peek", "mem.peek result=87", nullptr, processed},
    {"mem.str", "mem.str result=87", nullptr, processed},
    {"mem.scan", "mem.scan result=87", nullptr, processed},
    {"mem.deep", "mem.deep result=87", nullptr, processed},
    {"mem.watch 9 x", "mem.watch result=87", nullptr, processed},
    {"mem.stack", "mem.stack result=2", nullptr, processed},
    {"mem.hits", "mem.hits result=0 count=0 slots=0,0,0,0", nullptr, processed},
    {"arms.att", "arms.att result=2", nullptr, processed},
    {"arms.hide", "arms.hide result=2", nullptr, processed},
    {"arms.draw", "arms.draw result=0 (requested)", nullptr, processed},
    {"arms.free", "arms.free result=0", ArmsReport, processed},
    {"arms.report", "arms.report result=0 player=0x0 ikOwner=0x0 rig=0 weapon=0x0 character=0x0 attachment=0x0",
     ArmsReport, processed},
    {"wrist.report", "wrist.report result=0", WristReport, processed},
    {"wrist.anchor", "wrist.anchor result=0 anchor=1", nullptr, processed},
    {"wrist.anchor 0", "wrist.anchor result=0 anchor=0", nullptr, processed},
    {"wrist.anchor abc", "wrist.anchor result=0 anchor=1", nullptr, processed},   // the fallback, 1
    {"wrist.anchor 1", "wrist.anchor result=0 anchor=1", nullptr, processed},
    {"wrist.occlusion", "wrist.occlusion result=0 mask=3", nullptr, processed},
    {"wrist.occlusion 9", "wrist.occlusion result=0 mask=3", nullptr, processed},   // clamped
    {"wrist.force", "wrist.force result=0 force=0", nullptr, processed},
    {"wrist.face", "wrist.face result=0 face=1", nullptr, processed},
    {"wrist.haptic", "wrist.haptic result=0 haptic=1", nullptr, processed},
    {"wrist.native", "wrist.native result=0 scale=400 background=0", nullptr, processed},
    {"wrist.place", "wrist.place result=0", WristReport, processed},   // no bare form: re-stores, clamped
    {"wrist.gate", "wrist.gate result=0", WristReport, processed},

    // The dual, cull and hold rows.
    {"dual.report", "dual.report result=0", DualReports, processed},
    {"dual.observe", "dual.observe result=0", DualSceneReport, processed},
    {"dual.mode", "dual.mode result=0", DualSceneReport, processed},
    {"dual.ipd", "dual.ipd result=0", DualSceneReport, processed},
    {"dual.vr", "dual.vr result=0", DualSceneReport, processed},
    {"dual.overlay", "dual.overlay result=0", DualOverlayReport, processed},
    {"frame.probe", "frame.probe result=0", FrameProbeReport, processed},
    {"frame.probe 2", "frame.probe result=1", FrameProbeReport, processed},
    {"taa.report", "taa.report result=0 ", TaaReports, processed},
    {"taa.vr", "taa.vr result=0 ", TaaLaneReport, processed},   // bare: report; a value queues the AA mode
    {"cull.prepare", "cull.prepare result=0", CullPrepareReport, processed},
    // The two use-after-free watches were removed when the bugs were closed
    // pending recurrence (2026-10-27), and the frame probe left the cull verbs.
    {"cull.report", "result=rejected verb=\"cull.report\" detail=not_a_channel_verb_and_not_allowlisted", nullptr,
     rejected},
    {"cull.watch 1", "result=rejected verb=\"cull.watch\" detail=not_a_channel_verb_and_not_allowlisted", nullptr,
     rejected},
    {"cull.probe 1", "result=rejected verb=\"cull.probe\" detail=not_a_channel_verb_and_not_allowlisted", nullptr,
     rejected},
    {"rset.report", "result=rejected verb=\"rset.report\" detail=not_a_channel_verb_and_not_allowlisted", nullptr,
     rejected},
    {"rset.watch 1", "result=rejected verb=\"rset.watch\" detail=not_a_channel_verb_and_not_allowlisted", nullptr,
     rejected},
    {"window.hold", "window.hold result=0 note=no_value_nothing_changed", FocusHoldReport, processed},
    {"window.hold 2", "window.hold result=1", FocusHoldReport, processed},
    {"window.hold 0", "window.hold result=0", FocusHoldReport, processed},
    {"xr.hold", "xr.hold result=0 note=no_value_nothing_changed", XrHoldReport, processed},
    {"xr.hold 1 5", "xr.hold result=87", XrHoldReport, processed},
    {"xr.hold 2", "xr.hold result=87", XrHoldReport, processed},

    // The world reticle and the prompts. A malformed ui.prompts value is 1: on,
    // its Spec default.
    {"aim.world", "aim.world result=0", WorldReticleReports, processed},
    {"aim.world 2", "aim.world result=1", WorldReticleReports, processed},
    {"aim.reticlesize", "aim.reticlesize result=0", ReticleWorldReport, processed},
    {"aim.reticlesize 150", "aim.reticlesize result=0", ReticleWorldReport, processed},
    {"aim.reticlesize 10", "aim.reticlesize result=1", ReticleWorldReport, processed},
    {"aim.reticlesize abc", "aim.reticlesize result=0", ReticleWorldReport, processed},   // the default, 100
    {"ui.prompts", "ui.prompts result=0 enabled=0", ControllerPromptsReport, processed},
    {"ui.prompts 2", "ui.prompts result=1 enabled=0", ControllerPromptsReport, processed},
    {"ui.prompts abc", "ui.prompts result=0 enabled=1", ControllerPromptsReport, processed},
    {"ui.prompts 0", "ui.prompts result=0 enabled=0", ControllerPromptsReport, processed},

    // Contact, the surface stop, the swing and the wrench's draw.
    {"contact", "contact result=0", ContactReport, processed},
    {"contact log", "contact result=0", ContactReport, processed},
    {"contact log 0", "contact result=0", ContactReport, processed},
    {"surface", "surface result=0", SurfaceStopReport, processed},
    {"surface 2", "surface result=0", SurfaceStopReport, processed},             // not 0 or 1: ignored
    {"surface log 1", "surface result=0", SurfaceStopReport, processed},
    {"surface log 0", "surface result=0", SurfaceStopReport, processed},
    {"surface heavy abc", "surface result=0", SurfaceStopReport, processed},   // 10 kg, the Spec's
    {"surface heavy 10", "surface result=0", SurfaceStopReport, processed},
    {"swing", "swing result=0", SwingReport, processed},
    {"swing log 1", "swing result=0", SwingReport, processed},
    {"swing log 0", "swing result=0", SwingReport, processed},
    {"wrench.draw", "wrench.draw result=0", WrenchDrawReport, processed},
    {"wrench.draw abc", "wrench.draw result=0", WrenchDrawReport, processed},   // 1: on, the default
    {"wrench.sound", "wrench.sound result=0", WrenchSoundReport, processed},
    {"wrench.sound play", "wrench.sound result=0", WrenchSoundReport, processed},
    {"wrench.sound play 0", "wrench.sound result=0", WrenchSoundReport, processed},
    {"wrench.sound play abc", "wrench.sound result=0", WrenchSoundReport, processed},

    // The session, the menus and panels, and the rest. A bare setter only
    // reports; a malformed value is the verb's default (A9) unless the verb
    // refuses it.
    {"vr.options", "vr.options result=0", VrOptionsReports, processed},
    {"vr.options 0", "vr.options result=0", VrOptionsReports, processed},
    {"vr.status", "vr.status result=0 ", VrModeReport, processed},
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
    {"ui.text 0150", "ui.text result=0 cm=150", nullptr, processed},   // decimal: octal would be 104
    {"ui.text 0x96", "ui.text result=0 cm=140", nullptr, processed},   // hex is malformed: the default
    {"  ui.text\t\t150 ", "ui.text result=0 cm=150", nullptr, processed},
    {"ui.text 12abc", "ui.text result=0 cm=140", nullptr, processed},
    {"ui.text 140", "ui.text result=0 cm=140", nullptr, processed},
    {"ui.distance", "ui.distance result=0 cm=200", nullptr, processed},
    {"ui.distance 0310", "ui.distance result=0 cm=310", nullptr, processed},   // decimal, not octal 200
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
    {"view.height", "view.height result=0 ", HeadHeightReport, processed},
    {"view.recenter", "view.recenter result=1", nullptr, processed},
    {"view.apply", "view.apply result=0 note=no_value_nothing_changed", nullptr, processed},
    {"view.position", "view.position result=0 note=no_value_nothing_changed", nullptr, processed},
    {"near.fov", "near.fov result=0 deciDegrees=0 applied=0 refused=0 observedDeciDegrees=0 observedValid=0"
                 " faulted=0 thread=0", nullptr, processed},
    // A refused value leaves near.follow as it was (TestNearFovFollow).
    {"near.fov abc", "near.fov result=1 deciDegrees=0 applied=0 refused=0 observedDeciDegrees=0 observedValid=0"
                     " faulted=0 thread=0", nullptr, processed},
    {"near.fov -5", "near.fov result=1 deciDegrees=0 applied=0 refused=0 observedDeciDegrees=0 observedValid=0"
                    " faulted=0 thread=0", nullptr, processed},
    {"near.fov 1 2", "near.fov result=1 deciDegrees=0 applied=0 refused=0 observedDeciDegrees=0 observedValid=0"
                     " faulted=0 thread=0", nullptr, processed},
    {"near.follow 1", "near.follow result=0 followedDeci=0", nullptr, processed},
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
    {"ik.anchor 0", "result=rejected verb=\"ik.anchor\" detail=not_a_channel_verb_and_not_allowlisted", nullptr,
     rejected},
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
    {"aim.scene", "aim.scene result=0", SceneQueryReport, processed},
    {"haptics", "haptics result=0", HapticsReport, processed},
    {"aim.reticle", "aim.reticle result=0 note=no_value_nothing_changed", nullptr, processed},
    {"aim.reticleconverge", "aim.reticleconverge result=0 mm=10000", nullptr, processed},
    {"aim.reticleconverge 1000", "aim.reticleconverge result=0 mm=1000", nullptr, processed},
    {"aim.reticleconverge 10", "aim.reticleconverge result=0 mm=500", nullptr, processed},   // clamped
    {"aim.reticleconverge abc", "aim.reticleconverge result=0 mm=10000", nullptr, processed},
    {"aim.reticledispatch", "aim.reticledispatch result=0 note=no_value_nothing_changed", nullptr, processed},
    {"aim.reticlecanvas", "aim.reticlecanvas result=0 mode=1 native=0 nativeFailed=0", nullptr, processed},
    {"aim.reticlestage", "aim.reticlestage result=0 note=no_value_nothing_changed", nullptr, processed},
    {"aim.twohand", "aim.twohand result=0", TwoHandedAimStatus, processed},
    {"aim.bodyyaw 0", "result=rejected verb=\"aim.bodyyaw\" detail=not_a_channel_verb_and_not_allowlisted", nullptr,
     rejected},
    {"aim.origin", "aim.origin result=0 mode=0(native)", nullptr, processed},
    {"aim.calibratebarrel", "aim.calibratebarrel result=1 offsetMm=0,0,0", nullptr, processed},
    {"aim.enable", "aim.enable result=0 note=no_value_nothing_changed applied=0 nativeMagMilli=0", nullptr,
     processed},
    {"weapon.observe", "weapon.observe result=0 note=no_value_nothing_changed", nullptr, processed},
    {"hud.setvar path abc", "hud.setvar result=87", nullptr, processed},   // no default to fall back to
    {"hud.call fn abc", "hud.call result=87 queued=0 fn=fn", nullptr, processed},
    {"hud.call fn 1 abc", "hud.call result=87 queued=0 fn=fn", nullptr, processed},
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
    {"hud.probe abc 1", "hud.probe result=87 queued=0", nullptr, processed},
    {"xr.resolution", "xr.resolution result=0 recommended=0x0 max=0x0 backbuffer=0x0 heldEye=0x0 submitted=0x0"
                      " recommendedSamples=0 viewsDiffer=0 heldFormat=0 views=0 sizeMismatch=0", nullptr, processed},
    {"move.mode", "move.mode result=0 mode=0(off)", nullptr, processed},
    {"move.snap", "move.snap result=0 degrees=45", nullptr, processed},
    {"move.snap 30", "move.snap result=0 degrees=30", nullptr, processed},
    {"move.snap abc", "move.snap result=0 degrees=30", nullptr, processed},   // the current value
    {"move.snap 5", "move.snap result=1 degrees=30", nullptr, processed},
    {"move.snap 45", "move.snap result=0 degrees=45", nullptr, processed},
    {"move.headrelative", "move.headrelative result=0 enabled=1", nullptr, processed},
    {"move.headrelative abc", "move.headrelative result=0 enabled=1", nullptr, processed},   // the Spec's 1
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
//   dual.vr, frame.probe, cull.prepare; aim.shot, near.fx,
//   markers.depth, stars.depth, body.depth, ui.stereo and window.hold given 1; aim.beam and dual.capture
//   even bare; hud.var, hud.setvar, hud.call, hud.fit and hud.probe given
//   arguments that parse.
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
           "eyeLookupMiss", "nearDeltaUm", "nearPrevShifted", "nearPrevRefused"});
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
           "hudCalls", "hudRefused", "captures", "camYawMdeg", "headYawMdeg", "playYawMdeg", "eyeMm",
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

    // The log takes the report alone, not the replies to a command file's
    // earlier lines, which the command file step writes into the same stream.
    std::ostringstream batch;
    const std::string earlier = "ui.text result=0 cm=140\n";
    batch << earlier;
    Execute(Split("report"), batch);
    Require(gLines.size() == 2 && gLines.back() == "preyvr_report " + batch.str().substr(earlier.size()),
            "report logs only its own report");
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

// One rule for a number (CommandArgs.h, decision 2, 10-24): decimal, the whole
// word, or malformed -- and a malformed one changes nothing.
void TestNumbers()
{
    int i = -7;
    Require(ParseInt("310", i) && i == 310 && ParseInt("0310", i) && i == 310, "decimal, a leading zero included");
    Require(ParseInt("-5", i) && i == -5 && ParseInt("+5", i) && i == 5, "with a sign");
    i = -7;
    Require(!ParseInt("0x96", i) && !ParseInt("12abc", i) && !ParseInt("abc", i) && !ParseInt("", i) &&
                !ParseInt("99999999999", i) && i == -7,
            "hex, a partial number, a word, nothing and an overflow are malformed");
    Require(Arg({"v", "abc"}, 1, 9) == 9 && Arg({"v"}, 1, 9) == 9 && Arg({"v", "4"}, 1, 9) == 4,
            "Arg is the fallback when the word is malformed or missing");
    double d = -7;
    Require(ParseNumber("0.25", d) && d == 0.25 && ParseNumber("-1e2", d) && d == -100.0, "a fraction, an exponent");
    d = -7;
    Require(!ParseNumber("abc", d) && !ParseNumber("1.5x", d) && !ParseNumber("inf", d) && !ParseNumber("nan", d) &&
                !ParseNumber("0x1p3", d) && !ParseNumber("", d) && d == -7,
            "a word, a partial number, the non-finite and hex are malformed");
}

// near.fov (decision 4, 10-24): a refused value leaves near.follow as it was.
// Following is on from the start (the table's near.follow 1 leaves it on).
void TestNearFovFollow()
{
    Require(NearFovFollowing(), "near.follow is on");
    gLines.clear();
    for (const char* line : {"near.fov abc", "near.fov -5", "near.fov 1 2"}) {
        Require(Run(line).rfind("near.fov result=1 ", 0) == 0, "a value that does not parse is refused");
    }
    Require(gLines.empty() && NearFovFollowing(), "before anything changes");
    Require(Run("near.fov 5").rfind("near.fov result=1 ", 0) == 0 && NearFovFollowing(),
            "and a value the setter refuses puts following back");
}

// arm.capsule and arm.limbs set what arm.report shows; a refused line leaves
// it. Puts the defaults back.
void TestArmCapsule()
{
    Require(Run("arm.report").find(" capsuleMm=chest:30,-40,42,118;ribs:15,-45,41,116;waist:-15,-25,38,111;"
                                   "hips:-15,-40,50,117 limbsMm=50,45,40,30 avoidMm=10,10 avoidDegPerS=360") !=
                std::string::npos,
            "the defaults");
    Run("arm.capsule ribs -20 10 0 90");
    Run("arm.limbs 60 50 45 35");
    std::string report = Run("arm.report");
    Require(report.find(";ribs:-20,10,0,90;") != std::string::npos && report.find(" limbsMm=60,50,45,35") != std::string::npos,
            "set, as the report gives them");
    Run("arm.capsule ribs 0 0 400 90");
    Require(Run("arm.report").find(";ribs:-20,10,0,90;") != std::string::npos, "a refused capsule leaves it");
    Run("arm.capsule ribs 15 -45 41 116");
    Run("arm.limbs 50 45 40 30");
    report = Run("arm.report");
    Require(report.find(";ribs:15,-45,41,116;") != std::string::npos && report.find(" limbsMm=50,45,40,30") != std::string::npos,
            "the defaults are back");
}

// One torso (your 10-23 decision): body.torso and arm.torso set the one copy
// TorsoLane owns, which the arms' fallback filter uses. Refuses what arm.torso
// refuses. Puts the defaults (35 degrees, 600 per mille) back.
void TestTorsoOwner()
{
    float deadzone = 0.0f, relax = 0.0f;
    Run("body.torso 50 700");
    BodyTorsoSettings(deadzone, relax);
    Require(std::fabs(relax - 0.7f) < 1e-6f && std::fabs(deadzone - 50.0f * preyvr::kRadiansPerDegree) < 1e-6f,
            "body.torso sets the one torso");
    Run("arm.torso 40 650");
    BodyTorsoSettings(deadzone, relax);
    Require(std::fabs(relax - 0.65f) < 1e-6f && std::fabs(deadzone - 40.0f * preyvr::kRadiansPerDegree) < 1e-6f,
            "arm.torso sets the same one");
    Require(Run("arm.torso 181 600") == "arm.torso result=1" + ArmReport(), "arm.torso still refuses out of range");
    Require(Run("body.torso 181 600").rfind("body.torso result=87", 0) == 0, "and so does body.torso");
    BodyTorsoSettings(deadzone, relax);
    Require(std::fabs(deadzone - 40.0f * preyvr::kRadiansPerDegree) < 1e-6f, "a refused value leaves it alone");
    Run("arm.torso 35 600");
    BodyTorsoSettings(deadzone, relax);
    Require(std::fabs(deadzone - 35.0f * preyvr::kRadiansPerDegree) < 1e-6f && std::fabs(relax - 0.6f) < 1e-6f,
            "the defaults are back");
}

// contact log and swing log share one parse (EventLogSwitch): bare `log` turns
// the lane's per-event lines on, `log 0` off, a malformed value is the default
// (on), and another second word leaves them as they are.
void TestEventLogSwitches()
{
    const struct {
        const char* verb;
        unsigned (*get)();
    } lanes[] = {{"contact", ContactEventLog}, {"swing", SwingEventLog}};
    for (const auto& lane : lanes) {
        const std::string verb = lane.verb;
        Run((verb + " log").c_str());
        Require(lane.get() == 1, "bare log turns the lines on");
        Run((verb + " log 0").c_str());
        Require(lane.get() == 0, "log 0 turns them off");
        Run((verb + " log abc").c_str());
        Require(lane.get() == 1, "a malformed value is on");
        Run((verb + " other 0").c_str());
        Require(lane.get() == 1, "another word leaves them");
        Run((verb + " log 0").c_str());
    }
}

// The verb table against docs/TUNING-COMMANDS.md: every verb is documented,
// every command the doc's tables list is a verb (or a lane's own, under its
// prefix), and the verbs tagged for players are the ones the doc gives a player:
// the commands of the VR Options rows, the rows marked **Menu**, the
// player-facing table, and the VR session's own.
void TestVerbsDocumented()
{
    std::ifstream file(PREYVR_TUNING_DOC);
    Require(file.good(), "the tuning reference is readable");
    const auto names = [](const std::string& cell) {
        std::set<std::string> out;
        for (std::size_t open = cell.find('`'); open != std::string::npos; open = cell.find('`', open + 1)) {
            const std::size_t close = cell.find('`', open + 1);
            if (close == std::string::npos) { break; }
            const std::string token = cell.substr(open + 1, close - open - 1);
            const std::string name = token.substr(0, token.find(' '));
            if (!name.empty()) { out.insert(name); }
            open = close;
        }
        return out;
    };
    std::set<std::string> listed, players{"vr.enable", "vr.disable", "vr.status", "vr.recenter", "vr.options",
                                          "view.recenter", "view.calibrate"};
    std::string section, line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') { line.pop_back(); }
        if (line.rfind('#', 0) == 0) { section = line.substr(line.find_first_not_of("# ")); continue; }
        if (line.rfind('|', 0) != 0 || line.rfind("|---", 0) == 0 || line.rfind("| Command", 0) == 0 ||
            line.rfind("| Tab", 0) == 0) {
            continue;
        }
        std::vector<std::string> cells;
        std::string cell;
        for (std::size_t i = 1; i < line.size(); ++i) {
            if (line[i] == '\\' && i + 1 < line.size() && line[i + 1] == '|') { cell += '|'; ++i; continue; }
            if (line[i] == '|') { cells.push_back(cell.substr(cell.find_first_not_of(' ') == std::string::npos ? 0 : cell.find_first_not_of(' '))); cell.clear(); continue; }
            cell += line[i];
        }
        if (cells.empty()) { continue; }
        if (section == "Settings the VR Options menu saves") {
            if (cells.back().rfind("menu only", 0) == 0) { continue; }
            for (const auto& name : names(cells.back())) { listed.insert(name); players.insert(name); }
            continue;
        }
        const bool player = line.find("**Menu") != std::string::npos || section == "Player-facing commands not in the menu";
        for (const auto& name : names(cells.front())) {
            listed.insert(name);
            if (player) { players.insert(name); }
        }
    }
    unsigned failures = 0;
    std::set<std::string> verbs;
    for (const Verb& row : kVerbs) {
        verbs.insert(row.name);
        if (!listed.count(row.name)) { ++failures; std::cerr << "undocumented verb: " << row.name << '\n'; }
        if ((row.audience == Audience::player) != (players.count(row.name) > 0)) {
            ++failures;
            std::cerr << "audience: " << row.name << " is "
                      << (row.audience == Audience::player ? "player" : "not player") << " in the table\n";
        }
    }
    for (const auto& name : listed) {
        const bool lane = name.rfind("carry.", 0) == 0 || name.rfind("grenade.", 0) == 0 ||
                          name.rfind("arms.", 0) == 0 || name.rfind("mem.", 0) == 0 || name.rfind("wrist.", 0) == 0;
        if (!lane && !verbs.count(name)) { ++failures; std::cerr << "documented, but no such verb: " << name << '\n'; }
    }
    Require(failures == 0, "the verb table and the tuning reference agree");
}

} // namespace

int main()
{
    Require(GetModuleHandleW(L"PreyDll.dll") == nullptr, "no Prey in this process");
    Require(GetModuleHandleW(L"openxr_loader.dll") == nullptr, "and no OpenXR loader");
    const unsigned threads = ThreadCount();

    TestTable();
    TestNumbers();
    TestNearFovFollow();
    TestReport();
    TestLoggedReplies();
    TestCommandFile();
    TestTorsoOwner();
    TestEventLogSwitches();
    TestArmCapsule();
    TestVerbsDocumented();

    Require(GetModuleHandleW(L"openxr_loader.dll") == nullptr, "no command loaded the OpenXR loader");
    Require(preyvr::test::gMinHookCalls == 0, "no command tried to install a hook");
    Require(ThreadCount() <= threads, "no command started a thread");
    std::cout << "command channel: " << std::size(kCases) << " command lines pinned\n";
    return 0;
}
