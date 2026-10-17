#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// **Prey's live key bindings, read rather than guessed.** Which game action a
// gamepad key drives is in the `player` action map, whose defaults ship in
// Arkane-encrypted PAKs (ADDRESS_REGISTRY R-097). The live CActionMapManager can
// be read instead: its input-CRC-to-binding multimap (RE-H018-STATIC-GAPS §5)
// names every map, action and key. This is that walk, the C++ twin of
// tools/re/decode_h018_bind_snapshot.py, over any memory reader -- the DLL's
// guarded one, or a test's synthetic image.
//
// Fail closed: a vtable, back-pointer, tree or CRC that does not check out fails
// the whole read with a reason. A half-read list would look like "that key is
// bound to nothing", which is exactly the false answer this exists to avoid.
namespace preyvr::input {

// Steam PreyDll.dll 7D6E322F... (RE-H018-STATIC-GAPS §5; verify_h018_static_gaps.py).
struct ActionMapLayout {
    std::uintptr_t managerPointer = 0x248BCE0;   // qword: CActionMapManager*
    std::uintptr_t managerVtable = 0x1CBFC48;
    std::uintptr_t managerSecondaryVtable = 0x1CBFE08;   // at manager+8
    std::uintptr_t mapVtable = 0x1CBE4F0;
    std::uintptr_t actionVtable = 0x1CBE4A8;
};

struct BoundAction {
    std::string map, action, key;
    bool enabled = false;   // the manager and the map are enabled (filters not judged)
    // When the binding fires (SActionInput+0xC4, ADDRESS_REGISTRY): CryEngine's
    // EActionActivationMode bits -- 1 on press, 2 on release, 4 on hold,
    // 8 always, 16 retriggerable.
    std::uint32_t activation = 0;
};

class MemoryView {
public:
    virtual ~MemoryView() = default;
    // Copies `size` bytes from `address`; false if any of them cannot be read.
    virtual bool Read(std::uintptr_t address, void* out, std::size_t size) const = 0;
};

// Every binding the manager holds, in its CRC order. False, with `why`, when
// anything fails to check out; `out` is then empty.
bool ReadBindings(const MemoryView& memory, std::uintptr_t moduleBase, const ActionMapLayout& layout,
                  std::vector<BoundAction>& out, std::string& why);

// CRC-32 (zlib's), as the manager keys its multimap by the lowercase key name.
std::uint32_t Crc32(const std::string& text);

} // namespace preyvr::input
