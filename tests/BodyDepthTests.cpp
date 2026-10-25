// The body drawn in the world (preyvr/BodyDepth.h): which slot renders are
// taken out of the near pass, the words they read cleared of exactly the near
// bits, and those bits, and only those, put back after the render.
#include "preyvr/BodyDepth.h"
#include "preyvr/BodyDepthNative.h"
#include "preyvr/NativeContract.h"
#include "preyvr/NearParticles.h"
#include "TestSupport.h"

#include <iostream>

using namespace preyvr;
using namespace preyvr::bodydepth;
using preyvr::test::Require;
namespace bn = preyvr::bodydepth::native;

static_assert(bn::ObjectNearest == nearfx::kObjectNearest, "FOB_NEAREST is one bit, the near particles' too");
static_assert(bn::ObjectNearest == 1u << 23, "FOB_NEAREST is bit 23 (RenderCHR: bt rax,0x17)");
static_assert(bn::SlotRenderNearest == 1u << 1, "ENTITY_SLOT_RENDER_NEAREST is bit 1 (shr r12b,1; and r12b,1)");
static_assert(engine::CharacterInstanceLayout::drawNear == 0x2, "CS_FLAG_DRAW_NEAR (test byte [+0xAC8],2)");
static_assert(engine::PassInfoLayout::generalPassFlags == 0x2E5DF, "CSystem::Render's mov r8d,0x2E5DF");
static_assert(contract::Gated(bn::Contracts, bn::SlotRender), "the hooked slot render is the gated one");

namespace {

constexpr std::uintptr_t kPlayer = 0x1000, kOther = 0x2000;
constexpr std::uint32_t kGeneral = engine::PassInfoLayout::generalPassFlags;

// The player's slot as Prey leaves it: rendered (bit 0) and nearest, its object
// flags with FOB_NEAREST and others, the params' flags without it, and the
// character drawing its model (CS_FLAG_DRAW_MODEL, bit 0).
NearState PlayerSlot()
{
    NearState s{};
    s.slotFlags = 0x0003;
    s.slotObjectFlags = bn::ObjectNearest | 0x4000007u;
    s.paramsObjectFlags = 0x2000000000000100ull;
    s.characterFlags = 0x1;
    return s;
}

void TestDecide()
{
    Require(Decide(true, kPlayer, kPlayer, kGeneral) == Decision::world, "the player's slot in the main view");
    Require(Decide(false, kPlayer, kPlayer, kGeneral) == Decision::native, "off, every slot is Prey's");
    Require(Decide(true, kOther, kPlayer, kGeneral) == Decision::notPlayer, "another character's slot is Prey's");
    Require(Decide(true, 0, 0, kGeneral) == Decision::notPlayer, "no player known: nothing is taken");
    Require(Decide(true, kPlayer, 0, kGeneral) == Decision::notPlayer, "nor is a slot, with no player known");
    Require(Decide(true, kPlayer, kPlayer, 0x1234) == Decision::otherPass, "a shadow or other pass keeps the near pass");
    Require(Decide(true, kPlayer, kPlayer, kGeneral | 1u << 31) == Decision::otherPass, "the flags must be the main view's");
    Require(NearSlot(0x0002) && NearSlot(0x0003) && !NearSlot(0x0001) && !NearSlot(0xFFFD), "the near bit alone");
}

void TestInWorld()
{
    const NearState before = PlayerSlot();
    const NearState world = InWorld(before);
    Require(world.slotFlags == 0x0001, "the slot keeps ENTITY_SLOT_RENDER, loses only the nearest bit");
    Require(world.slotObjectFlags == 0x4000007u, "the object flags lose only FOB_NEAREST");
    Require(world.paramsObjectFlags == before.paramsObjectFlags, "params without FOB_NEAREST are left as they are");
    Require(world.characterFlags == 0x1, "the character keeps drawing its model");

    NearState all = before;
    all.paramsObjectFlags |= bn::ObjectNearest;
    all.characterFlags |= engine::CharacterInstanceLayout::drawNear;
    all.slotFlags = 0xFFFF;
    all.slotObjectFlags = 0xFFFFFFFFu;
    const NearState cleared = InWorld(all);
    Require(cleared.slotFlags == 0xFFFD && cleared.slotObjectFlags == 0xFF7FFFFFu, "every other slot bit kept");
    Require(cleared.paramsObjectFlags == (all.paramsObjectFlags & ~std::uint64_t{bn::ObjectNearest}),
            "the params' FOB_NEAREST cleared when they carry it");
    Require(cleared.characterFlags == 0x1, "CS_FLAG_DRAW_NEAR cleared when the character carries it");
    Require(!NearSlot(cleared.slotFlags), "nothing left that reads as near");
}

void TestRestored()
{
    const NearState before = PlayerSlot();
    // The render wrote meanwhile: a slot bit of its own and the params' flags
    // put back to the incoming value (0x9748A7).
    NearState now = InWorld(before);
    now.slotFlags |= 0x0100;
    now.paramsObjectFlags = before.paramsObjectFlags;
    const NearState back = Restored(now, before);
    Require(back.slotFlags == 0x0103, "the nearest bit back, the render's own bit kept");
    Require(back.slotObjectFlags == before.slotObjectFlags, "FOB_NEAREST back on the slot's object flags");
    Require(back.paramsObjectFlags == before.paramsObjectFlags, "params left without FOB_NEAREST, as they came");
    Require(back.characterFlags == before.characterFlags, "the character as it was");

    NearState all = before;
    all.paramsObjectFlags |= bn::ObjectNearest;
    all.characterFlags |= engine::CharacterInstanceLayout::drawNear;
    const NearState round = Restored(InWorld(all), all);
    Require(round.slotFlags == all.slotFlags && round.slotObjectFlags == all.slotObjectFlags &&
                round.paramsObjectFlags == all.paramsObjectFlags && round.characterFlags == all.characterFlags,
            "cleared then restored is what it was");

    // A bit that was clear before is never set.
    NearState plain{};
    plain.slotFlags = 0x0001;
    const NearState untouched = Restored(plain, plain);
    Require(untouched.slotFlags == 0x0001 && untouched.slotObjectFlags == 0 && untouched.paramsObjectFlags == 0 &&
                untouched.characterFlags == 0,
            "nothing added that was not there");
}

} // namespace

int main()
{
    TestDecide();
    TestInWorld();
    TestRestored();
    std::cout << "body_depth: the player's slot in the main view drawn without the near bits, and they come back\n";
    return 0;
}
