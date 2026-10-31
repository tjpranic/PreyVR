// The head lamp: the math (equipment/head/HeadLamp.h) against Prey's own composition,
// and the production detour with Prey replaced at its ABI boundary -- a fake
// PreyDll image holding the bone vtable's world placement and the entity
// system, the player's attachments, the view, the gate and the installer.
#include "equipment/head/HeadLampLane.cpp"
#include "DllTestSupport.h"
#include "TestSupport.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

PREYVR_TEST_CAPTURE_LOG()
PREYVR_TEST_STUB_MINHOOK()

// What the lane reaches outside its file, replaced below.
unsigned long long preyvr::dll::AnimIkOwnerCharacter() { return 0; }
std::uint64_t preyvr::dll::FindAttachment(std::uint64_t, std::string_view) { return 0; }
preyvr::dll::AppliedView preyvr::dll::LastAppliedView() { return {}; }
unsigned long preyvr::dll::InputDrainThreadId() { return 0; }

namespace headlamp_test {
using namespace preyvr;
using namespace preyvr::dll;
using preyvr::test::Check;
using preyvr::test::Logged;
namespace hn = preyvr::headlamp::native;

bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }
bool Near(Vec3 a, Vec3 b) { return Length(Sub(a, b)) < 1e-4f; }
// The same rotation (q and -q are).
bool Near(Quaternion a, Quaternion b)
{
    const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    return std::fabs(std::fabs(dot) - 1.0f) < 1e-4f;
}
Quaternion About(Vec3 axis, float degrees)
{
    const float h = degrees * 3.14159265f / 360.0f;
    const float n = Length(axis);
    return {axis.x / n * std::sin(h), axis.y / n * std::sin(h), axis.z / n * std::sin(h), std::cos(h)};
}

// "flashlight={active=1 ... psychoscopeMountMm=none}" with each field between spaces.
bool Reports(const char* field)
{
    std::string report = " " + HeadLampReport() + " ";
    for (char& c : report) {
        if (c == '{' || c == '}') { c = ' '; }
    }
    return report.find(std::string(" ") + field + " ") != std::string::npos;
}

// --- Prey's composition, written out as its bytes do it ------------------------------
// The bone update 0x7A3390: model = joint * relative, then the orientation * K.
// The world placement 0x7A2250: location * model, the model's offset scaled.
struct Rig {
    Pose location{About({0, 0, 1}, 40.0f), {300.0f, 740.0f, 480.0f}};
    float scale = 1.0f;
    Pose joint{About({1, 0, 0}, -12.0f), {0.0f, 0.04f, 1.72f}};   // camera_jnt, model space
};
Rig rig;
Pose Model(const Pose& relative, Quaternion k)
{
    return {Multiply(Multiply(rig.joint.orientation, relative.orientation), k),
            Add(rig.joint.position, Rotate(rig.joint.orientation, relative.position))};
}
Pose World(const Pose& relative, Quaternion k)
{
    const Pose model = Model(relative, k);
    return {Multiply(rig.location.orientation, model.orientation),
            Add(rig.location.position, Rotate(rig.location.orientation, Scale(model.position, rig.scale)))};
}

// The two attachments' relative defaults and Ks: `#camera` a little off the
// joint, the flashlight 9 cm right, 4 forward, 6 down, tipped down 5 degrees.
const Pose kEyeRelative{About({0, 0, 1}, 3.0f), {0.0f, 0.01f, 0.0f}};
const Quaternion kEyeK = About({0, 1, 0}, 2.0f);
const Pose kLightRelative{About({1, 0, 0}, -5.0f), {0.09f, 0.05f, -0.06f}};
const Quaternion kLightK = About({0, 0, 1}, -4.0f);

void TestMath()
{
    namespace hl = preyvr::headlamp;
    const Pose eyeOn = hl::OnJoint(kEyeRelative, kEyeK), lightOn = hl::OnJoint(kLightRelative, kLightK);
    const Pose mount = hl::Mount(eyeOn, lightOn);
    const Pose p{About({1, 2, 3}, 70.0f), {0.3f, -0.2f, 1.1f}};
    const Pose back = Compose(hl::Inverse(p), Compose(p, lightOn));
    Check(Near(back.position, lightOn.position) && Near(back.orientation, lightOn.orientation), "Inverse undoes a pose");

    // Two poses of the joint and two scales: from where Prey puts the light,
    // its eye is where Prey puts `#camera`, and on that eye the light is back
    // where Prey put it.
    for (const float scale : {1.0f, 1.25f}) {
        for (const Pose joint : {rig.joint, Pose{About({0, 1, 1}, 25.0f), {0.02f, -0.05f, 1.0f}}}) {
            rig.joint = joint;
            rig.scale = scale;
            const Pose light = World(kLightRelative, kLightK), eye = World(kEyeRelative, kEyeK);
            const Vec3 found = hl::EyeFromLight(light, scale, mount);
            Check(Near(found, eye.position), "Prey's eye found from its light, whatever the joint's pose and scale");
            const Pose again = hl::OnEye(eye.orientation, found, scale, mount);
            Check(Near(again.position, light.position) && Near(again.orientation, light.orientation),
                  "on Prey's own eye the light is where Prey placed it");
        }
    }
    rig = Rig{};

    // Looking 30 degrees down from 1 m away: the light keeps its mount on the eye.
    const Quaternion view = About({1, 0, 0}, -30.0f);
    const Vec3 eye{1.0f, 2.0f, 1.6f};
    const Pose on = hl::OnEye(view, eye, 1.0f, mount);
    Check(Near(on.orientation, Multiply(view, mount.orientation)), "the light turns with the view");
    Check(Near(Rotate(Conjugate(view), Sub(on.position, eye)), mount.position), "and sits at its mount on the eye");
}

// --- the fake PreyDll ---------------------------------------------------------------
constexpr std::size_t kImageSize = 0x2300000;
std::uintptr_t image = 0;
template <class T> void Put(std::uintptr_t address, const T& value)
{
    std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
}
template <class T> T Get(std::uintptr_t address)
{
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}

// The bone vtable's +0x70: Prey's world placement of the attachment.
const Placement* __fastcall FakeWorld(std::uintptr_t attachment, Placement* out)
{
    const Pose world = World(Get<Pose>(attachment + hn::AttachRelative), Get<Quaternion>(attachment + hn::AttachK));
    *out = {world.orientation, world.position, rig.scale};
    return out;
}

// The entity system and two entities, the lights. SetPosRotScale records.
constexpr std::uint32_t kFlashlightEntity = 41, kScopeEntity = 42;
struct FakeObject {
    std::uintptr_t vtable = 0;
};
std::uintptr_t systemTable[0x20]{}, entityTable[0x30]{};
FakeObject entitySystem, flashlightEntity, scopeEntity;
std::uintptr_t __fastcall FakeGetEntity(std::uintptr_t, std::uint32_t id)
{
    return id == kFlashlightEntity ? reinterpret_cast<std::uintptr_t>(&flashlightEntity)
           : id == kScopeEntity    ? reinterpret_cast<std::uintptr_t>(&scopeEntity)
                                   : 0;
}
struct Placed {
    std::uintptr_t entity = 0;
    Vec3 position{}, scale{};
    Quaternion rotation{};
    int flags = 0;
    unsigned count = 0;
} placed;
void __fastcall FakeSetPosRotScale(std::uintptr_t entity, const Vec3* position, const Quaternion* rotation,
                                   const Vec3* scale, int flags)
{
    placed = {entity, *position, *scale, *rotation, flags, placed.count + 1};
}

// The player's character, its attachment manager, and attachments laid out as
// Prey's bone attachments are.
alignas(16) std::uint8_t character[0x100]{}, manager[0x40]{}, otherCharacter[0x100]{}, otherManager[0x40]{};
alignas(16) std::uint8_t eyeAttachment[0x170]{}, lightAttachment[0x170]{}, scopeAttachment[0x170]{},
    otherAttachment[0x170]{};
alignas(16) std::uint8_t lightBinding[0x20]{}, scopeBinding[0x20]{};
char eyeName[] = "#camera", lightName[] = "flashlight", scopeName[] = "psychoscope_light";
char fxName[] = "flashlight_fx";
std::uintptr_t Address(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }
void Attachment(std::uint8_t* a, const char* name, const void* owner, const Pose& relative, Quaternion k, int joint)
{
    Put(Address(a), image + hn::BoneVtable);
    Put(Address(a) + hn::AttachName, Address(name));
    Put(Address(a) + hn::AttachManager, Address(owner));
    Put(Address(a) + hn::AttachRelative, relative);
    Put(Address(a) + hn::AttachK, k);
    Put(Address(a) + hn::AttachJoint, joint);
}
void Binding(std::uint8_t* b, std::uint32_t entity)
{
    Put(Address(b) + hn::BindingEntity, entity);
    Put(Address(b) + hn::BindingScale, Vec3{1.0f, 1.0f, 1.0f});
}
std::uint64_t FakePlayer() { return Address(character); }
std::uint64_t FakeFind(std::uint64_t owner, std::string_view name)
{
    return owner == Address(character) && name == "#camera" ? Address(eyeAttachment) : 0;
}

AppliedView view{};
AppliedView FakeView() { return view; }
bool mainThread = true;
bool FakeMainThread() { return mainThread; }

unsigned originals = 0;
void __fastcall FakeOriginal(void*, void*) { ++originals; }

unsigned resolves = 0, installs = 0;
DWORD readiness = ERROR_SUCCESS;
DWORD FakeResolve(std::uintptr_t& base)
{
    ++resolves;
    base = readiness ? 0 : image;
    return readiness;
}
bool FakeInstall(std::uintptr_t target)
{
    ++installs;
    Check(target == image + hn::ProcessAttachment, "the hook goes on the entity attachment's placement");
    gOriginal = FakeOriginal;
    return true;
}
void Retry()
{
    gInstallTried = false;
    gGate = "unchecked";
}

void Build()
{
    image = reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr, kImageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Check(image != 0, "the fake PreyDll is mapped");
    Put(image + hn::BoneVtable + hn::VtableWorldAbsolute, Address(reinterpret_cast<void*>(&FakeWorld)));
    systemTable[engine::EntitySystemLayout::vtableGetEntity / 8] = Address(reinterpret_cast<void*>(&FakeGetEntity));
    entityTable[hn::EntitySetPosRotScale / 8] = Address(reinterpret_cast<void*>(&FakeSetPosRotScale));
    entitySystem.vtable = Address(systemTable);
    flashlightEntity.vtable = scopeEntity.vtable = Address(entityTable);
    Put(image + engine::GlobalEnvironmentLayout::baseRva + engine::GlobalEnvironmentLayout::entitySystem,
        Address(&entitySystem));
    Put(Address(manager) + hn::ManagerCharacter, Address(character));
    Put(Address(otherManager) + hn::ManagerCharacter, Address(otherCharacter));
    Attachment(eyeAttachment, eyeName, manager, kEyeRelative, kEyeK, 98);
    Attachment(lightAttachment, lightName, manager, kLightRelative, kLightK, 98);
    Attachment(scopeAttachment, scopeName, manager, kLightRelative, kLightK, 98);
    Binding(lightBinding, kFlashlightEntity);
    Binding(scopeBinding, kScopeEntity);
    gPlayerCharacter = FakePlayer;
    gFindAttachment = FakeFind;
    gLastView = FakeView;
    gMainThread = FakeMainThread;
    gResolve = FakeResolve;
    gInstall = FakeInstall;
}

// Prey's attachment pass reaching a binding.
void Pass(std::uint8_t* binding, std::uint8_t* attachment) { ProcessAttachmentDetour(binding, attachment); }

// Where the light belongs on the view: Prey's eye moved by the view's offset,
// the light at its mount.
Pose Expected(const Pose& relative, Quaternion k)
{
    const Pose mount = headlamp::Mount(headlamp::OnJoint(kEyeRelative, kEyeK), headlamp::OnJoint(relative, k));
    const Pose eye = World(kEyeRelative, kEyeK);
    return headlamp::OnEye(view.rotation, Add(eye.position, view.offset), rig.scale, mount);
}
bool PlacedAt(const Pose& expected, std::uintptr_t entity)
{
    return placed.entity == entity && Near(placed.position, expected.position) &&
           Near(placed.rotation, expected.orientation) && placed.flags == hn::PlacementFlags &&
           Near(placed.scale, Vec3{rig.scale, rig.scale, rig.scale});
}

void TestLane()
{
    Build();
    Check(Reports("active=0") && Reports("hooks=0") && Reports("gate=unchecked") && Reports("flashlightMountMm=none"),
          "nothing hooked, nothing seen");

    // Not ready, refused, a hook that cannot be made, the launch switch.
    readiness = ERROR_NOT_READY;
    EnsureHeadLamp();
    Check(installs == 0 && Reports("gate=unchecked") && Logged("preyvr_head_lamp", "no_preydll"), "no PreyDll: not yet");
    readiness = ERROR_BAD_EXE_FORMAT;
    EnsureHeadLamp();
    EnsureHeadLamp();
    Check(installs == 0 && resolves == 2 && Reports("gate=refused") && Reports("active=0") &&
              Logged("result=unavailable", "bytes_mismatch", "EntityProcessAttachment"),
          "other bytes refuse the hook, for good");
    Retry();
    readiness = ERROR_SUCCESS;
    gInstall = Install;
    EnsureHeadLamp();
    Check(Reports("gate=hook_failed") && Reports("active=0") && Logged("result=failed", "create_hook"),
          "a hook that cannot be created is reported, and Prey's lights kept");
    Retry();
    gInstall = FakeInstall;
    SetEnvironmentVariableW(L"PREYVR_HEAD_LAMP", L"0");
    EnsureHeadLamp();
    SetEnvironmentVariableW(L"PREYVR_HEAD_LAMP", nullptr);
    Check(installs == 0 && Reports("active=0") && Logged("disabled_by_environment"), "the launch switch leaves it unhooked");
    EnsureHeadLamp();
    EnsureHeadLamp();
    Check(installs == 1 && Reports("hooks=1") && Reports("gate=ok") && Reports("active=1") &&
              Logged("hook_installed", "EntityProcessAttachment", "rows=3"),
          "VR start hooks the placement, once");

    // This frame's view, looking down and left, the head 3 cm right of Prey's eye.
    view = {Multiply(About({0, 0, 1}, 55.0f), About({1, 0, 0}, -35.0f)), {0.03f, 0.0f, -0.01f}, 1};
    Pass(lightBinding, lightAttachment);
    Check(originals == 0 && placed.count == 1 && PlacedAt(Expected(kLightRelative, kLightK), Address(&flashlightEntity)),
          "the flashlight goes on the headset's eye, at its mount, instead of where Prey puts it");
    const Vec3 mm = Scale(
        headlamp::Mount(headlamp::OnJoint(kEyeRelative, kEyeK), headlamp::OnJoint(kLightRelative, kLightK)).position,
        1000.0f);
    std::ostringstream field, logged;
    field << "flashlightMountMm=" << std::lround(mm.x) << "," << std::lround(mm.y) << "," << std::lround(mm.z);
    logged.precision(1);
    logged << std::fixed << "mm=" << mm.x << "," << mm.y << "," << mm.z;
    Check(Reports("placed=1") && Reports("flashlight=1") && Reports(field.str().c_str()) &&
              Logged("preyvr_head_lamp", "mount light=flashlight", logged.str().c_str()),
          "the report counts it and gives its mount on the eye");
    Check(std::fabs(mm.x - 90.0f) < 6.0f && std::fabs(mm.y - 40.0f) < 6.0f && std::fabs(mm.z + 60.0f) < 6.0f,
          "a mount near the joint offsets, turned only by the eye's few degrees");
    Pass(lightBinding, lightAttachment);
    Check(preyvr::test::Count("mount light=flashlight") == 1, "the mount is logged once, not every frame");
    Pass(scopeBinding, scopeAttachment);
    Check(originals == 0 && PlacedAt(Expected(kLightRelative, kLightK), Address(&scopeEntity)) && Reports("psychoscope=1"),
          "and the psychoscope's light");

    // Anything else is Prey's.
    Attachment(otherAttachment, lightName, otherManager, kLightRelative, kLightK, 98);
    Pass(lightBinding, otherAttachment);
    Attachment(otherAttachment, fxName, manager, kLightRelative, kLightK, 98);
    Pass(lightBinding, otherAttachment);
    Attachment(otherAttachment, lightName, manager, kLightRelative, kLightK, 98);
    Put(Address(otherAttachment), image + 0x1D20000);
    Pass(lightBinding, otherAttachment);
    Check(originals == 3 && placed.count == 3, "another character's light, another name, another attachment type: Prey's");
    gPlayerCharacter = [] { return std::uint64_t{0}; };
    Pass(lightBinding, lightAttachment);
    gPlayerCharacter = FakePlayer;
    Check(originals == 4 && placed.count == 3, "no player character known: Prey's");

    // The attachment pass before this frame's view: Prey's until the view, then on it.
    EndHeadLampFrame();
    Pass(lightBinding, lightAttachment);
    Check(originals == 5 && placed.count == 3 && Reports("deferred=1"), "before the frame's view: Prey's placement for now");
    view = {About({0, 1, 0}, 20.0f), {0.0f, 0.02f, 0.0f}, 2};
    PlaceHeadLampsBeforeRender();
    Check(placed.count == 4 && PlacedAt(Expected(kLightRelative, kLightK), Address(&flashlightEntity)) &&
              Reports("late=1"),
          "and on the view before the render");
    PlaceHeadLampsBeforeRender();
    Check(placed.count == 4, "once");
    // No view at all that frame (VR not driving it): Prey's stays.
    EndHeadLampFrame();
    Pass(lightBinding, lightAttachment);
    PlaceHeadLampsBeforeRender();
    Check(originals == 6 && placed.count == 4 && Reports("native=1"), "no view this frame: Prey's placement");
    // A light still waiting when the frame ends is dropped.
    Pass(lightBinding, lightAttachment);
    EndHeadLampFrame();
    view.sequence = 3;
    PlaceHeadLampsBeforeRender();
    Check(placed.count == 4, "a frame's lights do not wait into the next");

    // Off the main thread, unreadable, unplaceable: Prey's.
    mainThread = false;
    Pass(lightBinding, lightAttachment);
    mainThread = true;
    Check(originals == 8 && Reports("offThread=1"), "off the main thread: Prey's");
    Put(Address(eyeAttachment) + hn::AttachJoint, 95);
    Pass(lightBinding, lightAttachment);
    Put(Address(eyeAttachment) + hn::AttachJoint, 98);
    Check(originals == 9 && Reports("unread=1"), "the eye on another joint: no mount, Prey's");
    view.rotation = {0.0f, 0.0f, 0.0f, 0.0f};
    Pass(lightBinding, lightAttachment);
    view.rotation = Quaternion{};
    Check(originals == 10 && Reports("unplaced=1"), "a degenerate view: Prey's");
    Binding(lightBinding, 77);
    const unsigned before = placed.count;
    Pass(lightBinding, lightAttachment);
    Binding(lightBinding, kFlashlightEntity);
    Check(originals == 10 && placed.count == before, "no entity bound: nothing to place, as in Prey's");

    // Scaled character: the mount scales with it.
    rig.scale = 1.25f;
    Pass(lightBinding, lightAttachment);
    Check(PlacedAt(Expected(kLightRelative, kLightK), Address(&flashlightEntity)), "a scaled character's light, scaled mount");
    rig.scale = 1.0f;

    // VR stopped: Prey's lights.
    StopHeadLamp();
    Pass(lightBinding, lightAttachment);
    Check(originals == 11 && Reports("active=0"), "VR stopped: Prey's placement");
    EnsureHeadLamp();
    view.sequence = 4;
    Pass(lightBinding, lightAttachment);
    Check(originals == 11 && Reports("active=1"), "VR again: on the eye again");
}

} // namespace headlamp_test

int main()
{
    headlamp_test::TestMath();
    headlamp_test::TestLane();
    std::cout << "head_lamp: the flashlight and the psychoscope's light on the headset's eye, at Prey's mount\n";
}
