#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace preyvr::options {
// **Every VR Options setting.** The save file names each by its Spec key, so
// these ids are this build's alone and their order is free to change.
enum Setting : unsigned { Turn, HeadRelative, TurnSpeed, TwoHand, GripToggle, AutoSnap, AutoSnapAttach, AutoSnapBreak,
    Psychoscope, UiScale, Holsters, Wrist, WristSize,
    ReticleSize, MarkerSize, Haptics, HapticStrength, FreeSwing, SwingSpeed, HeavySwingSpeed, PhysicalContact, PushStrength,
    PsiTarget, Medkit, BeltHints, DebugOverlay,
    UiDistance, UiOffsetRight, UiOffsetUp, UiText, PointerHand,
    MoveDeadzone, TurnDeadzone, StrafeSpeed, BackwardSpeed,
    CarryHold, GrenadeHand, CarryThrowStrength, GrenadeThrowStrength,
    WristTick, WristAlong, WristHover, WristTilt,
    CarryHand,
    FingerCurl, AimLasers, ShotTrail, ArmLength, SurfaceStop, HeavyMass, LeanZone, HipsBack, NearClip,
    ScreenPullback, Antialiasing, Helmet, HelmetHeight, HelmetForward, HelmetSize, ZeroGRoll, ZeroGPitch,
    Count };
// What a value counts, for Display. The directional units name the side instead
// of a sign.
enum class Unit : unsigned char { none, percent, cm, mm, degrees, degreesRightLeft, degreesUpDown,
    cmPerSecond, kg, mmUpDown, mmForwardBack };
// **One setting, described once**: the key it is saved under, its label and
// help, its default, range and step, and how its value reads -- `names` for a
// named choice (one per value, from `min`), `choices` for a fixed set of
// values, `zero` for what 0 reads as, `unit` for the rest. A 0..1 setting with
// none of these is on/off. The panel edits each as what it is (KindOf).
struct Spec {
    const char* key;
    const wchar_t* label;
    const wchar_t* help;
    int initial, min, max, step;
    Unit unit = Unit::none;
    std::span<const wchar_t* const> names{};
    std::span<const int> choices{};
    const wchar_t* zero = nullptr;
};
const Spec& Describe(unsigned id);
// In range; with fixed choices, the first or any whole value from the second
// up -- a command can set turning to 75 degrees, but 1..14 is no angle at all.
bool Valid(unsigned id, int value);
enum class Kind : unsigned char { toggle, choice, range };
Kind KindOf(unsigned id);
// A choice's values, in order: its fixed choices, or each named value.
std::vector<int> Choices(unsigned id);
std::wstring Display(unsigned id, int value);
using Values = std::array<int, Count>;
// Which settings a saved file holds. Only those are applied, so a setting
// never chosen in the menu keeps its built-in value.
using Present = std::array<bool, Count>;
Values Defaults();
// One setting's default, its Spec's `initial`. Every lane starts at its own
// setting's default but three, which start off until VR start sets them to it
// (VrMode.cpp); vr_options_wiring pins both.
int Default(unsigned id);
// `present` null writes every setting; otherwise only those marked.
std::string Serialize(const Values&, const Present* present = nullptr);
// Transactional parse: malformed/unknown fields refuse the whole file. Keys
// the file lacks take their defaults in `result` and are unmarked in `present`.
bool Parse(std::string_view text, Values& result, Present* present = nullptr);
// The panel's size in pixels: its texture, and the laser's mapping onto it.
constexpr unsigned Width = 1400, Height = 1100;
// **The panel's buttons**, which act rather than set: the negative entries of
// MenuEntries, beside the settings.
enum Button : int { RecenterView = -1, ClearHolsterSlots = -2, CalibratePosture = -3 };
// **The panel's layout**: its tabs and the rows on each, any number per tab
// (the list scrolls). Every setting appears exactly once, each button once.
unsigned MenuPages();
const wchar_t* MenuPageName(unsigned page);
std::span<const int> MenuEntries(unsigned page);
// The tab with no settings, where the panel lists what each action is bound to.
unsigned ControlsPage();
// The controls, as the state machine needs them: the opening hold, B, and
// whether everything else is at rest -- no press may carry into or out of the
// panel. `x`/`y` are the panel's navigation (MenuInput::navigate). `chord`:
// both grips are held, so Menu is the recenter chord -- neither a pause tap nor
// the opening hold.
struct Input {
    bool valid = false, modal = false, start = false, accept = false, cancel = false;
    bool previous = false, next = false, trigger = false, chord = false;
    float x = 0, y = 0, dt = 0;
    std::uint64_t epoch = 0;
};
// What the panel did: a setting given `value` (refused unless Valid), or one
// of its buttons.
struct Change {
    int setting = -1;
    int value = 0;
    bool recenter = false, clearHolsters = false, calibrate = false;
};
// **When VR Options is open, and who has the controls.** A long hold of left
// Menu (or RequestOpen) opens it over whatever is shown: the running game or a
// game menu alike -- the game is not paused for it. A short tap is still the
// game's own pause (PauseTap). If a game menu opens or closes under it, it
// closes: it was opened over the other. It owns the controls while open and
// until they are at rest after, arms only once they are at rest after opening,
// and closes on B. The panel (VrOptionsUi) does everything else.
class Menu {
public:
    void Update(const Input&);
    bool Open() const { return open_; }
    // Opened over a game menu (true) or the running game.
    bool OverMenu() const { return overMenu_; }
    // Open, and the controls have been at rest since it opened: only then may
    // a press act on it (the hold that opened it must not also click).
    bool Armed() const { return open_ && armed_; }
    bool OwnsInput() const { return open_ || quarantine_; }
    bool PauseTap() const { return pauseTap_; }
    void RequestOpen() { requested_ = true; }
    void Close();
private:
    bool open_ = false, quarantine_ = false, armed_ = false, requested_ = false;
    bool startArmed_ = false, startUsed_ = false;
    bool startWas_ = false, pauseTap_ = false, overMenu_ = false;
    float hold_ = 0;
    std::uint64_t epoch_ = 0;
};
}
