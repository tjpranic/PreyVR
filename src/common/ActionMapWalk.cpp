#include "preyvr/ActionMapWalk.h"

#include <cctype>
#include <set>
#include <utility>

namespace preyvr::input {
namespace {

constexpr std::uint64_t kMaxNodes = 8192;

// One read failure anywhere fails the walk; the first reason is kept.
class Reader {
public:
    Reader(const MemoryView& memory, std::string& why) : memory_(memory), why_(why) {}
    std::uint64_t Q(std::uintptr_t address) { std::uint64_t v = 0; Get(address, &v, sizeof(v)); return v; }
    std::uint32_t U(std::uintptr_t address) { std::uint32_t v = 0; Get(address, &v, sizeof(v)); return v; }
    std::uint8_t B(std::uintptr_t address) { std::uint8_t v = 0; Get(address, &v, sizeof(v)); return v; }
    std::string String(std::uintptr_t address)
    {
        std::string text;
        if (address == 0) { Fail("null string"); return text; }
        for (std::size_t i = 0; i < 256 && ok_; ++i) {
            const char c = static_cast<char>(B(address + i));
            if (c == '\0') { return text; }
            if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) {
                Fail("string not printable ASCII");
                return {};
            }
            text.push_back(c);
        }
        Fail("unterminated string");
        return {};
    }
    // An MSVC std::map/multimap red-black tree, in order: left +0, parent +8,
    // right +0x10, is-nil byte +0x19; the head is the sentinel (root at +8,
    // minimum at +0, maximum at +0x10).
    std::vector<std::uintptr_t> Tree(std::uintptr_t head, std::uint64_t count)
    {
        std::vector<std::uintptr_t> ordered;
        if (head == 0 || count > kMaxNodes || B(head + 0x19) != 1) { Fail("tree head or count"); return {}; }
        struct Step { std::uintptr_t node, parent; bool emit; };
        std::vector<Step> stack{{static_cast<std::uintptr_t>(Q(head + 8)), head, false}};
        std::set<std::uintptr_t> seen;
        while (!stack.empty() && ok_) {
            const Step step = stack.back();
            stack.pop_back();
            if (step.node == head) { continue; }
            if (step.emit) { ordered.push_back(step.node); continue; }
            if (step.node == 0 || seen.count(step.node) != 0 || seen.size() >= count || B(step.node + 0x19) != 0 ||
                Q(step.node + 8) != step.parent) {
                Fail("tree node: nil, cycle, count or parent");
                return {};
            }
            seen.insert(step.node);
            stack.push_back({static_cast<std::uintptr_t>(Q(step.node + 0x10)), step.node, false});
            stack.push_back({step.node, step.parent, true});
            stack.push_back({static_cast<std::uintptr_t>(Q(step.node)), step.node, false});
        }
        if (!ok_) { return {}; }
        if (ordered.size() != count) { Fail("tree count does not match its nodes"); return {}; }
        const std::uintptr_t first = ordered.empty() ? head : ordered.front();
        const std::uintptr_t last = ordered.empty() ? head : ordered.back();
        if (Q(head) != first || Q(head + 0x10) != last) { Fail("tree extrema disagree with its nodes"); return {}; }
        return ordered;
    }
    void Fail(const char* reason)
    {
        if (ok_) { why_ = reason; }
        ok_ = false;
    }
    bool Ok() const { return ok_; }

private:
    void Get(std::uintptr_t address, void* out, std::size_t size)
    {
        if (ok_ && !memory_.Read(address, out, size)) { Fail("unreadable memory"); }
    }
    const MemoryView& memory_;
    std::string& why_;
    bool ok_ = true;
};

} // namespace

std::uint32_t Crc32(const std::string& text)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const unsigned char c : text) {
        crc ^= c;
        for (int bit = 0; bit < 8; ++bit) { crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u))); }
    }
    return ~crc;
}

bool ReadBindings(const MemoryView& memory, std::uintptr_t base, const ActionMapLayout& layout,
                  std::vector<BoundAction>& out, std::string& why)
{
    out.clear();
    why.clear();
    Reader r(memory, why);
    const std::uintptr_t manager = r.Q(base + layout.managerPointer);
    if (!r.Ok()) { return false; }
    if (manager == 0) { why = "no action map manager yet"; return false; }
    if (r.Q(manager) != base + layout.managerVtable || r.Q(manager + 8) != base + layout.managerSecondaryVtable) {
        r.Fail("manager vtables do not match this build");
        return false;
    }
    const bool managerEnabled = r.B(manager + 0xDC) != 0;
    std::vector<BoundAction> rows;
    std::uint32_t previous = 0;
    for (const std::uintptr_t node : r.Tree(r.Q(manager + 0x58), r.Q(manager + 0x60))) {
        const std::uint32_t crc = r.U(node + 0x20);
        const std::uintptr_t input = r.Q(node + 0x28), action = r.Q(node + 0x30), map = r.Q(node + 0x38);
        if (!r.Ok()) { break; }
        if (r.Q(map) != base + layout.mapVtable || r.Q(action) != base + layout.actionVtable ||
            r.Q(map + 0x10) != manager || r.Q(action + 0x60) != map) {
            r.Fail("binding map/action vtable or back-pointer");
            break;
        }
        BoundAction row;
        row.key = r.String(r.Q(input + 0x18));
        row.action = r.String(r.Q(action + 0x40));
        row.map = r.String(r.Q(map + 0x58));
        if (!r.Ok()) { break; }
        std::string lower = row.key;
        for (char& c : lower) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
        if (row.key.empty() || crc != Crc32(lower) || r.U(input + 0x98) != crc) {
            r.Fail("binding key does not match its CRC");
            break;
        }
        if (!rows.empty() && crc < previous) { r.Fail("binding tree out of CRC order"); break; }
        previous = crc;
        row.enabled = managerEnabled && r.B(map + 8) != 0;
        row.activation = r.U(input + 0xC4);
        row.mapAddress = map;
        if (!r.Ok()) { break; }
        rows.push_back(std::move(row));
    }
    if (!r.Ok()) { return false; }
    out = std::move(rows);
    return true;
}

bool ReadActionMapEnabled(const MemoryView& memory, std::uintptr_t base, const ActionMapLayout& layout,
                          std::uintptr_t map, const std::string& name, bool& enabled, std::string& why)
{
    enabled = false;
    why.clear();
    Reader r(memory, why);
    const std::uintptr_t manager = r.Q(base + layout.managerPointer);
    if (!r.Ok()) { return false; }
    if (manager == 0) { why = "no action map manager yet"; return false; }
    if (r.Q(manager) != base + layout.managerVtable || r.Q(manager + 8) != base + layout.managerSecondaryVtable) {
        r.Fail("manager vtables do not match this build");
        return false;
    }
    if (map == 0 || r.Q(map) != base + layout.mapVtable || r.Q(map + 0x10) != manager) {
        r.Fail("map vtable or back-pointer");
        return false;
    }
    const std::string mapName = r.String(r.Q(map + 0x58));
    if (r.Ok() && mapName != name) { r.Fail("map name"); }
    const bool on = r.B(manager + 0xDC) != 0 && r.B(map + 8) != 0;
    if (!r.Ok()) { return false; }
    enabled = on;
    return true;
}

} // namespace preyvr::input
