// The live action-map walk (ActionMapWalk) over a hand-made memory image: the
// topology of tools/re/decode_h018_bind_snapshot.py's fixture. Synthetic data,
// never a claim about Prey's shipped bindings.
#include "preyvr/ActionMapWalk.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <vector>

using namespace preyvr::input;

namespace {
void Check(bool ok, const char* why)
{
    if (!ok) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); }
}

class Image final : public MemoryView {
public:
    explicit Image(std::uintptr_t start, std::size_t size) : start_(start), bytes_(size) {}
    bool Read(std::uintptr_t address, void* out, std::size_t size) const override
    {
        if (address < start_ || address + size > start_ + bytes_.size()) { return false; }
        std::memcpy(out, bytes_.data() + (address - start_), size);
        return true;
    }
    void Q(std::uintptr_t a, std::uint64_t v) { std::memcpy(&bytes_[a - start_], &v, 8); }
    void U(std::uintptr_t a, std::uint32_t v) { std::memcpy(&bytes_[a - start_], &v, 4); }
    void B(std::uintptr_t a, std::uint8_t v) { bytes_[a - start_] = v; }
    void S(std::uintptr_t a, const char* text) { std::memcpy(&bytes_[a - start_], text, std::strlen(text) + 1); }
    void Empty(std::uintptr_t head)
    {
        for (unsigned o : {0u, 8u, 0x10u}) { Q(head + o, head); }
        B(head + 0x19, 1);
    }

private:
    std::uintptr_t start_;
    std::vector<unsigned char> bytes_;
};

// The image starts at the "module base" and holds the manager pointer, the
// vtable addresses it is checked against, and every object.
constexpr std::uintptr_t kBase = 0x10000;
constexpr std::uintptr_t kManager = 0x20000;

struct Fixture {
    Image image{kBase, 0x20000};
    ActionMapLayout layout{};
    std::uintptr_t first = 0x21200, second = 0x21300, head = 0x21000, map = 0x22000;
    std::uintptr_t input[2] = {0x22800, 0x22900}, action[2] = {0x22200, 0x22300};
    Fixture()
    {
        layout.managerPointer = 0x100;
        layout.managerVtable = 0x200;
        layout.managerSecondaryVtable = 0x208;
        layout.mapVtable = 0x210;
        layout.actionVtable = 0x218;
        auto& m = image;
        m.Q(kBase + layout.managerPointer, kManager);
        m.Q(kManager, kBase + layout.managerVtable);
        m.Q(kManager + 8, kBase + layout.managerSecondaryVtable);
        m.B(kManager + 0xDC, 1);
        // Two bindings, one key: the multimap allows it (a key in two maps).
        m.Q(kManager + 0x58, head);
        m.Q(kManager + 0x60, 2);
        m.Empty(head);
        m.Q(head, first); m.Q(head + 8, first); m.Q(head + 0x10, second);
        m.Q(first, head); m.Q(first + 8, head); m.Q(first + 0x10, second);
        m.Q(second, head); m.Q(second + 8, first); m.Q(second + 0x10, head);
        m.Q(map, kBase + layout.mapVtable); m.B(map + 8, 1); m.Q(map + 0x10, kManager);
        m.Q(map + 0x58, 0x24000); m.S(0x24000, "player");
        m.S(0x24100, "xi_thumbl");
        const std::uint32_t crc = Crc32("xi_thumbl");
        const char* names[] = {"sprint", "fixture_other"};
        for (int i = 0; i < 2; ++i) {
            const std::uintptr_t node = i == 0 ? first : second, name = 0x24200 + i * 0x100;
            m.Q(node + 0x28, input[i]); m.Q(node + 0x30, action[i]); m.Q(node + 0x38, map); m.U(node + 0x20, crc);
            m.Q(action[i], kBase + layout.actionVtable); m.Q(action[i] + 0x40, name); m.Q(action[i] + 0x60, map);
            m.S(name, names[i]);
            m.Q(input[i] + 0x18, 0x24100); m.U(input[i] + 0x98, crc); m.U(input[i] + 0xC4, i == 0 ? 1u : 5u);
        }
    }
    bool Run(std::vector<BoundAction>& out, std::string& why) const
    {
        return ReadBindings(image, kBase, layout, out, why);
    }
    bool Refused() const
    {
        std::vector<BoundAction> out;
        std::string why;
        return !Run(out, why) && out.empty() && !why.empty();
    }
};
} // namespace

int main()
{
    Check(Crc32("") == 0 && Crc32("123456789") == 0xCBF43926u, "zlib's CRC-32");
    {
        Fixture f;
        std::vector<BoundAction> out;
        std::string why;
        Check(f.Run(out, why), "a well-formed manager reads");
        Check(out.size() == 2 && out[0].map == "player" && out[0].action == "sprint" && out[0].key == "xi_thumbl" &&
              out[1].action == "fixture_other" && out[0].enabled, "both bindings of one key, in order, enabled");
        Check(out[0].activation == 1 && out[1].activation == 5, "each binding's activation mode: on press; on press or hold");
        f.image.B(f.map + 8, 0);
        Check(f.Run(out, why) && !out[0].enabled, "a disabled map reads as disabled");
        f.image.B(f.map + 8, 1);
        f.image.B(kManager + 0xDC, 0);
        Check(f.Run(out, why) && !out[0].enabled, "and so does a disabled manager");
    }
    // **Fail closed**: each malformation refuses the whole read.
    { Fixture f; f.image.Q(kBase + f.layout.managerPointer, 0); Check(f.Refused(), "no manager yet"); }
    { Fixture f; f.image.Q(kManager, 0x1234); Check(f.Refused(), "a manager vtable from another build"); }
    { Fixture f; f.image.Q(kManager + 0x60, 3); Check(f.Refused(), "a count the tree does not have"); }
    { Fixture f; f.image.Q(f.second + 0x10, f.first); Check(f.Refused(), "a cycle"); }
    { Fixture f; f.image.Q(f.second + 8, f.head); Check(f.Refused(), "a wrong parent"); }
    { Fixture f; f.image.U(f.first + 0x20, Crc32("xi_thumbl") ^ 1); Check(f.Refused(), "a key that is not its CRC"); }
    { Fixture f; f.image.Q(f.input[0] + 0x18, 0x7FFFFFFF); Check(f.Refused(), "an unreadable key name"); }
    { Fixture f; f.image.Q(f.map + 0x10, kManager + 1); Check(f.Refused(), "a map that is not this manager's"); }
    { Fixture f; f.image.Q(f.action[1], 0x1234); Check(f.Refused(), "an action vtable from another build"); }
    { Fixture f; f.image.Q(f.head + 0x10, f.first); Check(f.Refused(), "extrema that disagree with the nodes"); }
    {
        // An empty multimap is a valid, empty answer.
        Fixture f;
        f.image.Empty(f.head);
        f.image.Q(kManager + 0x60, 0);
        std::vector<BoundAction> out;
        std::string why;
        Check(f.Run(out, why) && out.empty(), "no bindings is an answer, not a failure");
    }
    std::cout << "action map walk: bindings read, every malformation refused\n";
    return 0;
}
