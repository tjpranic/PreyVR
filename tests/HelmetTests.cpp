// The zero-G helmet: the math (preyvr/Helmet.h) against Prey's own composition,
// and the lane with Prey replaced at its boundary -- the player's attachments,
// the view, where the player is drawn, and the gate.
#include "../src/dll/Helmet.cpp"
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

namespace helmet_test {
using namespace preyvr;
using namespace preyvr::dll;
using preyvr::test::Check;
using preyvr::test::Logged;
namespace hm = preyvr::helmet::native;
namespace hn = preyvr::headlamp::native;

bool Near(Vec3 a, Vec3 b) { return Length(Sub(a, b)) < 1e-4f; }
// The same rotation (q and -q are).
bool Near(Quaternion a, Quaternion b)
{
    const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    return std::fabs(std::fabs(dot) - 1.0f) < 1e-4f;
}
bool Near(const Pose& a, const Pose& b) { return Near(a.position, b.position) && Near(a.orientation, b.orientation); }
Quaternion About(Vec3 axis, float degrees)
{
    const float h = degrees * 3.14159265f / 360.0f;
    const float n = Length(axis);
    return {axis.x / n * std::sin(h), axis.y / n * std::sin(h), axis.z / n * std::sin(h), std::cos(h)};
}

// "helmet={active=1 ... eyeGapMaxDeg=0.0}" with each field between spaces.
bool Reports(const char* field)
{
    std::string report = " " + HelmetReport() + " ";
    for (char& c : report) {
        if (c == '{' || c == '}') { c = ' '; }
    }
    return report.find(std::string(" ") + field + " ") != std::string::npos;
}

// --- Prey's composition, as its bytes do it ---------------------------------------
// The bone update 0x7A3390: model = joint * relative, then the orientation * K.
// The render: the character's location * model, the model's offset scaled.
struct Rig {
    Pose location{About({0, 0, 1}, 40.0f), {300.0f, 740.0f, 480.0f}};
    float scale = 1.0f;
    Pose joint{About({1, 0, 0}, -12.0f), {0.0f, 0.04f, 1.72f}};   // camera_jnt, model space
};
Rig rig;
Pose Model(const Pose& relative, Quaternion k)
{
    return Compose(rig.joint, headlamp::OnJoint(relative, k));
}

// `#camera` a little off the joint; the helmet's K a few degrees off.
const Pose kEyeRelative{About({0, 0, 1}, 3.0f), {0.0f, 0.01f, 0.03f}};
const Quaternion kEyeK = About({0, 1, 0}, 2.0f);
const Quaternion kHelmetK = About({0, 0, 1}, -4.0f);
// Prey's sway, which the helmet on the head leaves out.
const Pose kSway{About({1, 1, 0}, 6.0f), {}};

void TestMath()
{
    namespace hl = preyvr::helmet;
    const Pose mount = hl::Mount(headlamp::OnJoint(kEyeRelative, kEyeK), kHelmetK);
    // On Prey's own eye the helmet is where Prey draws it with no sway, whatever
    // the joint, the location and the scale.
    for (const float scale : {1.0f, 1.25f}) {
        for (const Pose joint : {rig.joint, Pose{About({0, 1, 1}, 25.0f), {0.02f, -0.05f, 1.0f}}}) {
            for (const Pose location : {rig.location, Pose{About({1, 0, 1}, -70.0f), {-5.0f, 2.0f, -1.0f}}}) {
                rig = {location, scale, joint};
                const Pose eyeModel = Model(kEyeRelative, kEyeK);
                const Pose eye = hl::InWorld(location, scale, eyeModel);
                const Pose onEye = hl::EyeInModel(location.orientation, scale, eyeModel, eye.orientation, {});
                Check(Near(onEye, eyeModel) && Near(hl::OnEye(onEye, mount), Model(Pose{}, kHelmetK)),
                      "on Prey's eye the helmet is Prey's, without the sway");
            }
        }
    }
    rig = Rig{};

    // The headset's eye looking up and right, 30 cm from Prey's: the helmet
    // keeps its mount on that eye, wherever the character is drawn.
    const Quaternion view = Multiply(About({0, 0, 1}, -60.0f), About({1, 0, 0}, 35.0f));
    const Vec3 offset{0.2f, -0.1f, 0.2f};
    for (const float scale : {1.0f, 1.25f}) {
        rig.scale = scale;
        const Pose eyeModel = Model(kEyeRelative, kEyeK);
        const Pose model = hl::OnEye(hl::EyeInModel(rig.location.orientation, scale, eyeModel, view, offset), mount);
        const Pose world = hl::InWorld(rig.location, scale, model);
        const Vec3 eye = Add(hl::InWorld(rig.location, scale, eyeModel).position, offset);
        Check(Near(world.orientation, Multiply(view, mount.orientation)), "the helmet turns with the head");
        Check(Near(Rotate(Conjugate(view), Sub(world.position, eye)), Scale(mount.position, scale)),
              "and sits at its mount on the head, scaled with the character");
    }
    rig = Rig{};

    // A Matrix34 back to its pose and scale.
    const Pose pose{About({1, -2, 3}, 115.0f), {4.0f, -5.0f, 6.0f}};
    for (const float scale : {1.0f, 0.8f}) {
        const Vec3 x = Scale(Rotate(pose.orientation, {1, 0, 0}), scale), y = Scale(Rotate(pose.orientation, {0, 1, 0}), scale),
                   z = Scale(Rotate(pose.orientation, {0, 0, 1}), scale);
        const float m[12] = {x.x, y.x, z.x, pose.position.x, x.y, y.y, z.y, pose.position.y, x.z, y.z, z.z, pose.position.z};
        Pose read{};
        float s = 0.0f;
        Check(hl::FromMatrix(m, read, s) && Near(read, pose) && std::fabs(s - scale) < 1e-5f,
              "an entity's world matrix is its pose and scale");
    }
    const float empty[12]{};
    Pose read{};
    float s = 0.0f;
    Check(!hl::FromMatrix(empty, read, s), "a matrix with no rotation is refused");
}

// --- the fake Prey -----------------------------------------------------------------
constexpr std::uintptr_t kBase = 0x180000000ull;
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
std::uintptr_t Address(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }

alignas(16) std::uint8_t character[0x40]{}, helmetAttachment[0x170]{}, eyeAttachment[0x170]{};
// The helmet's own character, held by a character attachment, and another character.
alignas(16) std::uint8_t helmetObject[0x20]{}, helmetCharacter[0x20]{}, otherCharacter[0x20]{};
// Prey's flags on the helmet: its shadow hidden, and bits that are not hiding.
constexpr std::uint32_t kPreyFlags = 0x20000 | 0x5;
void Attachment(std::uint8_t* a, const Pose& relative, Quaternion k, int joint)
{
    Put(Address(a), kBase + hn::BoneVtable);
    Put(Address(a) + hn::AttachRelative, relative);
    Put(Address(a) + hn::AttachK, k);
    Put(Address(a) + hn::AttachJoint, joint);
    Put(Address(a) + hm::AttachModel, Model(relative, k));
}
Pose HelmetModel() { return Get<Pose>(Address(helmetAttachment) + hm::AttachModel); }
std::uint32_t HelmetFlags() { return Get<std::uint32_t>(Address(helmetAttachment) + hm::AttachFlags); }

bool haveHelmet = true, locationKnown = true;
std::uint64_t FakePlayer() { return Address(character); }
std::uint64_t FakeFind(std::uint64_t owner, std::string_view name)
{
    if (owner != Address(character)) { return 0; }
    return name == "helmet" && haveHelmet ? Address(helmetAttachment) : name == "#camera" ? Address(eyeAttachment) : 0;
}
AppliedView view{};
AppliedView FakeView() { return view; }
bool FakeLocation(Pose& location, float& scale)
{
    location = rig.location;
    scale = rig.scale;
    return locationKnown;
}

unsigned resolves = 0;
DWORD readiness = ERROR_SUCCESS;
DWORD FakeResolve(std::uintptr_t& base)
{
    ++resolves;
    base = readiness ? 0 : kBase;
    return readiness;
}
// The character render: what each character was drawn with.
struct Drawn {
    std::uintptr_t character = 0;
    float offset[8]{};
    unsigned count = 0;
} rendered;
void __fastcall FakeRender(void* who, void*, const float* offset, void*, std::uintptr_t)
{
    rendered.character = Address(who);
    std::memcpy(rendered.offset, offset, sizeof(rendered.offset));
    ++rendered.count;
}
bool scaleInstalls = true;
unsigned installs = 0;
bool FakeInstallScale(std::uintptr_t target)
{
    ++installs;
    Check(target == kBase + hm::CharacterRender, "the size hook goes on the character render");
    gRenderOriginal = FakeRender;
    return scaleInstalls;
}
// A character drawn as Prey's attachment draws it: an identity offset.
void Render(std::uint8_t* who)
{
    const float identity[8] = {0, 0, 0, 1, 0, 0, 0, 1};
    RenderDetour(who, nullptr, identity, nullptr, 0);
}

void Retry()
{
    gGateTried = false;
    gBase = 0;
    gGate = "unchecked";
}

void Build()
{
    Attachment(eyeAttachment, kEyeRelative, kEyeK, 98);
    Attachment(helmetAttachment, kSway, kHelmetK, 98);
    Put(Address(helmetAttachment) + hm::AttachFlags, kPreyFlags);
    gPlayerCharacter = FakePlayer;
    gFindAttachment = FakeFind;
    gLastView = FakeView;
    gPlayerLocation = FakeLocation;
    gResolve = FakeResolve;
    gInstallScale = FakeInstallScale;
    Put(Address(helmetAttachment) + hm::AttachObject, Address(helmetObject));
    Put(Address(helmetObject), kBase + hm::ChrAttachmentVtable);
    Put(Address(helmetObject) + hm::ObjectCharacter, Address(helmetCharacter));
}

// Prey's eye as the body draws it, and as Prey composes the view: its position
// relative to the player entity, as in first person. The headset's eye this
// frame: up and left of it, the head 3 cm right and 1 cm down.
Pose DrawnEye() { return helmet::InWorld(rig.location, rig.scale, Model(kEyeRelative, kEyeK)); }
void NewView(unsigned long long sequence)
{
    const Pose drawn = DrawnEye();
    const Pose prey{drawn.orientation, Sub(drawn.position, rig.location.position)};
    view = {Multiply(prey.orientation, Multiply(About({0, 0, 1}, 50.0f), About({1, 0, 0}, 30.0f))),
            {0.03f, 0.0f, -0.01f}, sequence, prey};
}
Pose Expected()
{
    const Pose mount = helmet::Mount(headlamp::OnJoint(kEyeRelative, kEyeK), kHelmetK);
    return helmet::OnEye(
        helmet::EyeInModel(rig.location.orientation, rig.scale, Model(kEyeRelative, kEyeK), view.rotation, view.offset),
        mount);
}
// A frame: the helmet before the render, the render, then after it.
void Frame()
{
    PlaceHelmetBeforeRender();
    EndHelmetFrame();
}

void TestLane()
{
    Build();
    Check(Reports("active=0") && Reports("gate=unchecked") && Reports("shown=1") && Reports("mountMm=none"),
          "nothing checked, nothing seen");
    NewView(1);
    PlaceHelmetBeforeRender();
    Check(Near(HelmetModel(), Model(kSway, kHelmetK)), "before VR start: Prey's helmet");

    // Not ready, refused, the launch switch.
    readiness = ERROR_NOT_READY;
    EnsureHelmet();
    Check(Reports("gate=unchecked") && Reports("active=0") && Logged("preyvr_helmet", "no_preydll"), "no PreyDll: not yet");
    readiness = ERROR_BAD_EXE_FORMAT;
    EnsureHelmet();
    EnsureHelmet();
    Check(resolves == 2 && Reports("gate=refused") && Reports("active=0") &&
              Logged("result=unavailable", "bytes_mismatch", "HelmetAttachment"),
          "other bytes refuse it, for good");
    PlaceHelmetBeforeRender();
    Check(Near(HelmetModel(), Model(kSway, kHelmetK)), "refused: Prey's helmet");
    Retry();
    readiness = ERROR_SUCCESS;
    SetEnvironmentVariableW(L"PREYVR_HELMET", L"0");
    EnsureHelmet();
    SetEnvironmentVariableW(L"PREYVR_HELMET", nullptr);
    Check(Reports("active=0") && Logged("preyvr_helmet", "disabled_by_environment"), "the launch switch leaves Prey's");
    EnsureHelmet();
    EnsureHelmet();
    Check(Reports("gate=ok") && Reports("active=1") && Logged("gate_ok", "HelmetAttachment", "rows=6"),
          "VR start checks the gate, once");

    // This frame's view: the helmet on the headset's eye, without Prey's sway.
    PlaceHelmetBeforeRender();
    Check(Near(HelmetModel(), Expected()) && Reports("placed=1"),
          "the helmet's model transform puts it on the headset's eye, at Prey's mount");
    const Pose world = helmet::InWorld(rig.location, rig.scale, HelmetModel());
    const Pose mount = helmet::Mount(headlamp::OnJoint(kEyeRelative, kEyeK), kHelmetK);
    Check(Near(Rotate(Conjugate(view.rotation), Sub(world.position, Add(DrawnEye().position, view.offset))), mount.position) &&
              Near(Multiply(Conjugate(view.rotation), world.orientation), mount.orientation),
          "drawn, it sits on the head as it sits on Prey's eye");
    EndHelmetFrame();
    const Vec3 mm = Scale(mount.position, 1000.0f);
    std::ostringstream field;
    field << "mountMm=" << std::lround(mm.x) << "," << std::lround(mm.y) << "," << std::lround(mm.z);
    Check(Reports("kept=1") && Reports(field.str().c_str()) && Logged("preyvr_helmet", "mount mm=") &&
              Reports("eyeGapMm=0.0") && Reports("eyeGapDeg=0.0"),
          "the render left it; the mount is reported; Prey's eye is the drawn `#camera`");
    Check(std::fabs(mm.x) < 5.0f && std::fabs(mm.y + 10.0f) < 5.0f && std::fabs(mm.z + 30.0f) < 5.0f,
          "the helmet's mount: the joint, seen from the eye");

    // The frame's view already rendered: VR is not driving this one.
    Put(Address(helmetAttachment) + hm::AttachModel, Model(kSway, kHelmetK));
    Frame();
    Check(Near(HelmetModel(), Model(kSway, kHelmetK)) && Reports("native=1"), "no view this frame: Prey's helmet");
    Check(preyvr::test::Count("mount mm=") == 1, "the mount is logged once, not every frame");

    // Prey has it put away: left alone.
    Put(Address(helmetAttachment) + hm::AttachFlags, kPreyFlags | hm::HideBits);
    NewView(2);
    Frame();
    Check(Near(HelmetModel(), Model(kSway, kHelmetK)) && Reports("stowed=1") &&
              HelmetFlags() == (kPreyFlags | hm::HideBits),
          "a helmet Prey has hidden: untouched");
    Put(Address(helmetAttachment) + hm::AttachFlags, kPreyFlags);

    // Hidden in VR Options: hidden from the render, Prey's bits back after.
    SetHelmetShown(0);
    Check(HelmetShown() == 0 && Reports("shown=0") && Logged("preyvr_helmet", "shown value=0"), "the option reads back");
    PlaceHelmetBeforeRender();
    Check(HelmetFlags() == (kPreyFlags | hm::HideBits) && Reports("hidden=1"), "hidden for the render");
    EndHelmetFrame();
    Check(HelmetFlags() == kPreyFlags && Near(HelmetModel(), Model(kSway, kHelmetK)),
          "and Prey's own bits back after it, the helmet unmoved");
    SetHelmetShown(1);

    // Something in the render made its transform again.
    NewView(3);
    PlaceHelmetBeforeRender();
    Put(Address(helmetAttachment) + hm::AttachModel, Model(kSway, kHelmetK));
    EndHelmetFrame();
    Check(Reports("placed=2") && Reports("overwritten=1"), "a transform made again in the render is reported");

    // Prey's eye 5 mm off the drawn `#camera` (a camera effect): reported, and
    // the helmet, which takes only the view's rotation and offset, unmoved by it.
    NewView(4);
    view.prey.position = Add(view.prey.position, {0.005f, 0.0f, 0.0f});
    Frame();
    Check(Reports("eyeGapMm=5.0") && Reports("eyeGapMaxMm=5.0") && Near(HelmetModel(), Expected()),
          "Prey's eye off the drawn eye is reported, and the helmet still goes on the headset's eye");

    // No helmet, the eye on another joint, no location, a degenerate view.
    haveHelmet = false;
    NewView(5);
    Frame();
    haveHelmet = true;
    Check(Reports("noHelmet=1"), "no helmet on the character: nothing");
    Put(Address(eyeAttachment) + hn::AttachJoint, 95);
    NewView(6);
    Frame();
    Put(Address(eyeAttachment) + hn::AttachJoint, 98);
    Check(Reports("unread=1"), "the eye on another joint: no mount");
    locationKnown = false;
    NewView(7);
    Frame();
    locationKnown = true;
    NewView(8);
    view.rotation = {0.0f, 0.0f, 0.0f, 0.0f};
    Frame();
    Check(Reports("unread=3") && Reports("placed=3"), "no place for the character, or no view: nothing written");

    // A scaled character: the mount scales with it.
    rig.scale = 1.25f;
    NewView(9);
    PlaceHelmetBeforeRender();
    Check(Near(HelmetModel(), Expected()), "a scaled character's helmet");
    EndHelmetFrame();
    rig.scale = 1.0f;

    // Height and forward / back: along the head's own axes, in metres.
    SetHelmetHeightMm(-20);
    SetHelmetForwardMm(-30);
    for (const float scale : {1.0f, 1.25f}) {
        rig.scale = scale;
        NewView(20 + static_cast<unsigned long long>(scale * 4));
        Frame();
        const Pose shifted = helmet::InWorld(rig.location, rig.scale, HelmetModel());
        const Vec3 onHead = Rotate(Conjugate(view.rotation), Sub(shifted.position, Add(DrawnEye().position, view.offset)));
        Check(Near(onHead, Add(Vec3{0.0f, -0.03f, -0.02f}, Scale(mount.position, scale))) &&
                  Near(Multiply(Conjugate(view.rotation), shifted.orientation), mount.orientation),
              "lowered 2 cm and moved back 3 cm along the head, whatever the character's scale");
    }
    rig.scale = 1.0f;
    Check(HelmetHeightMm() == -20 && HelmetForwardMm() == -30 && Reports("heightMm=-20") && Reports("forwardMm=-30"),
          "the position reads back and is reported");
    SetHelmetHeightMm(0);
    SetHelmetForwardMm(0);

    // Size: the helmet's character drawn larger, every other character as Prey draws it.
    Check(Reports("sizeHook=1") && installs == 1, "the size hook went on with the gate");
    Render(helmetCharacter);
    Check(rendered.count == 1 && rendered.offset[7] == 1.0f && Reports("sized=0"), "at Prey's size, the helmet as Prey draws it");
    SetHelmetSizePercent(130);
    NewView(30);
    PlaceHelmetBeforeRender();
    Render(helmetCharacter);
    Check(rendered.character == Address(helmetCharacter) && std::fabs(rendered.offset[7] - 1.3f) < 1e-6f &&
              rendered.offset[3] == 1.0f && rendered.offset[4] == 0.0f && Reports("sized=1") && Reports("sizedDraws=1") &&
              Reports("sizePct=130"),
          "at 130%, the helmet's character drawn at 1.3, its offset otherwise Prey's");
    Render(otherCharacter);
    Check(rendered.character == Address(otherCharacter) && rendered.offset[7] == 1.0f && Reports("sizedDraws=1"),
          "any other character as Prey draws it");
    EndHelmetFrame();
    Put(Address(helmetObject), kBase + 0x1CB13B0);
    NewView(31);
    PlaceHelmetBeforeRender();
    Render(helmetCharacter);
    EndHelmetFrame();
    Put(Address(helmetObject), kBase + hm::ChrAttachmentVtable);
    Check(rendered.offset[7] == 1.0f && Reports("sized=0"), "a helmet whose object is not a character attachment: Prey's size");
    SetHelmetShown(0);
    NewView(32);
    PlaceHelmetBeforeRender();
    Render(helmetCharacter);
    EndHelmetFrame();
    SetHelmetShown(1);
    Check(rendered.offset[7] == 1.0f && Reports("sized=0"), "hidden: nothing sized");

    // VR stopped while hidden: Prey's bits back at once, then Prey's helmet.
    SetHelmetShown(0);
    NewView(10);
    PlaceHelmetBeforeRender();
    StopHelmet();
    Check(HelmetFlags() == kPreyFlags && Reports("active=0"), "VR stop gives Prey's hide bits back");
    SetHelmetShown(1);
    Put(Address(helmetAttachment) + hm::AttachModel, Model(kSway, kHelmetK));
    NewView(11);
    Frame();
    Check(Near(HelmetModel(), Model(kSway, kHelmetK)), "VR stopped: Prey's helmet");
    EnsureHelmet();
    NewView(12);
    Frame();
    Check(Reports("active=1") && Near(HelmetModel(), Expected()), "VR again: on the head again");
    Check(Reports("sized=1"), "and sized again");
    StopHelmet();
    Render(helmetCharacter);
    Check(rendered.offset[7] == 1.0f && Reports("sized=0"), "VR stopped: Prey's size at once");

    // A size hook that cannot be made: the helmet still goes on the head, at Prey's size.
    Retry();
    scaleInstalls = false;
    EnsureHelmet();
    NewView(40);
    PlaceHelmetBeforeRender();
    EndHelmetFrame();
    Check(Reports("gate=ok") && Reports("sizeHook=0") && Reports("sized=0") && Near(HelmetModel(), Expected()),
          "no size hook: on the head, Prey's size");
    SetHelmetSizePercent(100);
}

} // namespace helmet_test

int main()
{
    helmet_test::TestMath();
    helmet_test::TestLane();
    std::cout << "helmet: the zero-G helmet on the headset's eye at Prey's mount, or hidden from the render\n";
}
