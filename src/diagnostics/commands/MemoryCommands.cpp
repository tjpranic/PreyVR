#include "diagnostics/commands/MemoryCommands.h"
#include "diagnostics/commands/CommandArgs.h"
#include "engine/SafeMemory.h"
#include "engine/EngineAccess.h"

#include "aim/GameplayFrame.h"
#include "body/ik/AnimIkTakeover.h"
#include "diagnostics/capture/DebugWatch.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>

namespace preyvr::dll {
namespace {

bool ParseHex(const std::string& text, std::uint64_t& value)
{
    try {
        std::size_t used = 0;
        value = std::stoull(text, &used, 16);
        return used == text.size();
    } catch (...) {
        return false;
    }
}

bool Copy(std::uint64_t address, void* out, std::size_t size)
{
    return SafeCopy(out, reinterpret_cast<const void*>(address), size);
}

}  // namespace

bool ResolveAddress(const std::string& text, std::uint64_t& value)
{
    std::string base = text;
    std::uint64_t offset = 0;
    if (const auto plus = text.find('+'); plus != std::string::npos) {
        base = text.substr(0, plus);
        if (!ParseHex(text.substr(plus + 1), offset)) { return false; }
    }
    if (base == "player") {
        GameplayPoseFrame frame{};
        if (!TryGetGameplayPoseFrame(frame, false) || frame.player == 0) { return false; }
        value = frame.player;
    } else if (base == "arms") {
        value = AnimIkOwnerCharacter();
    } else if (base == "dll") {
        value = PreyBase();
    } else if (!ParseHex(base, value)) {
        return false;
    }
    if (value == 0) { return false; }
    value += offset;
    return true;
}

bool ExecuteMemoryCommand(const std::vector<std::string>& args, std::ostringstream& out)
{
    const std::string& verb = args[0];
    if (verb.rfind("mem.", 0) != 0) { return false; }
    const std::uint64_t dll = PreyBase();
    if (verb == "mem.peek") {
        // mem.peek <addr> [bytes 64]: qwords, with the PreyDll RVA of any that point into it.
        std::uint64_t address = 0, size = 64;
        if (args.size() < 2 || !ResolveAddress(args[1], address)) { out << "mem.peek result=87"; return true; }
        if (args.size() > 2) { ParseHex(args[2], size); }
        size = (std::min<std::uint64_t>)(size, 0x1000) & ~7ull;
        out << "mem.peek result=0 at=0x" << std::hex << address;
        for (std::uint64_t o = 0; o < size; o += 8) {
            std::uint64_t q = 0;
            if (!Copy(address + o, &q, 8)) { out << " +" << o << "=??"; break; }
            out << "\n+" << std::setw(4) << std::setfill('0') << o << " " << std::setw(16) << q;
            float f[2];
            std::memcpy(f, &q, 8);
            out << std::dec << "  f=" << f[0] << "," << f[1] << std::hex;
            if (dll && q >= dll && q < dll + 0x3000000) { out << "  rva=0x" << (q - dll); }
        }
        out << std::dec;
    } else if (verb == "mem.scan") {
        // mem.scan <addr> <bytes> <value>: offsets of 8-byte-aligned qwords equal to value.
        std::uint64_t address = 0, size = 0, value = 0;
        if (args.size() < 4 || !ResolveAddress(args[1], address) || !ParseHex(args[2], size) || !ResolveAddress(args[3], value)) {
            out << "mem.scan result=87";
            return true;
        }
        size = (std::min<std::uint64_t>)(size, 0x100000);
        out << "mem.scan result=0 at=0x" << std::hex << address << " value=0x" << value << " hits=";
        unsigned hits = 0;
        for (std::uint64_t o = 0; o + 8 <= size; o += 8) {
            std::uint64_t q = 0;
            if (!Copy(address + o, &q, 8)) { out << "[fault+" << o << "]"; break; }
            if (q == value && hits++ < 64) { out << "+" << o << " "; }
        }
        out << std::dec << " count=" << hits;
    } else if (verb == "mem.deep") {
        // mem.deep <addr> <bytes> <innerBytes> <value>: every pointer in the outer block whose
        // target holds value within innerBytes; prints outer+inner offsets.
        std::uint64_t address = 0, size = 0, inner = 0, value = 0;
        if (args.size() < 5 || !ResolveAddress(args[1], address) || !ParseHex(args[2], size) || !ParseHex(args[3], inner) ||
            !ResolveAddress(args[4], value)) {
            out << "mem.deep result=87";
            return true;
        }
        size = (std::min<std::uint64_t>)(size, 0x10000);
        inner = (std::min<std::uint64_t>)(inner, 0x4000);
        out << "mem.deep result=0 value=0x" << std::hex << value << " hits=";
        unsigned hits = 0;
        for (std::uint64_t o = 0; o + 8 <= size; o += 8) {
            std::uint64_t p = 0;
            if (!Copy(address + o, &p, 8)) { break; }
            if (p < 0x10000 || (p & 7) != 0 || p > 0x7FFFFFFFFFFF) { continue; }
            if (p == value) { out << "+" << o << "=self "; continue; }
            for (std::uint64_t i = 0; i + 8 <= inner; i += 8) {
                std::uint64_t q = 0;
                if (!Copy(p + i, &q, 8)) { break; }
                if (q == value && hits++ < 64) { out << "+" << o << "->0x" << p << "+" << i << " "; }
            }
        }
        out << std::dec << " count=" << hits;
    } else if (verb == "mem.watch") {
        // mem.watch <slot> <addr> <w4|w8|x> [captures 64]; mem.watch <slot> off
        const unsigned slot = static_cast<unsigned>(Arg(args, 1, 99));
        if (args.size() > 2 && args[2] == "off") {
            out << "mem.watch result=" << DisarmWatch(slot);
            return true;
        }
        std::uint64_t address = 0;
        if (args.size() < 4 || slot > 3 || !ResolveAddress(args[2], address)) { out << "mem.watch result=87"; return true; }
        const WatchKind kind = args[3] == "x" ? WatchKind::execute : args[3] == "w8" ? WatchKind::write8 : WatchKind::write4;
        const unsigned captures = static_cast<unsigned>(Arg(args, 4, 64));
        out << "mem.watch result=" << ArmWatch(slot, address, kind, captures) << " at=0x" << std::hex << address
            << std::dec;
    } else if (verb == "mem.hits") {
        // mem.hits [clear]: every capture, RIP as an RVA, with rcx/rdx/r8 and the stack top.
        out << "mem.hits result=0 count=" << WatchCaptureCount() << " slots=" << WatchHitCount(0) << ","
            << WatchHitCount(1) << "," << WatchHitCount(2) << "," << WatchHitCount(3) << std::hex;
        for (unsigned i = 0; i < WatchCaptureCount() && i < 64; ++i) {
            WatchCapture c{};
            if (!ReadWatchCapture(i, c)) { continue; }
            out << "\n s" << c.slot << " rip=" << (c.rip >= dll ? c.rip - dll : c.rip) << " rcx=" << c.rcx
                << " rdx=" << c.rdx << " r8=" << c.r8 << " rbx=" << c.rbx << " rdi=" << c.rdi << " rsi=" << c.rsi
                << " top=" << (c.stackTop >= dll && c.stackTop < dll + 0x3000000 ? c.stackTop - dll : c.stackTop)
                << " tid=" << std::dec << c.threadId << std::hex;
        }
        out << std::dec;
        if (args.size() > 1 && args[1] == "clear") { ClearWatchCaptures(); }
    } else if (verb == "mem.stack") {
        // mem.stack <slotCapture>: return-address candidates (PreyDll RVAs) on the stack of capture N.
        const unsigned index = static_cast<unsigned>(Arg(args, 1, 0));
        WatchCapture c{};
        if (!ReadWatchCapture(index, c)) { out << "mem.stack result=2"; return true; }
        out << "mem.stack result=0 (stack may have moved since the trap)" << std::hex;
        for (unsigned i = 0; i < 64; ++i) {
            std::uint64_t q = 0;
            if (!Copy(c.rsp + i * 8, &q, 8)) { break; }
            if (q >= dll && q < dll + 0x1E00000) { out << " +" << i * 8 << ":" << (q - dll); }
        }
        out << std::dec;
    } else if (verb == "mem.str") {
        std::uint64_t address = 0;
        if (args.size() < 2 || !ResolveAddress(args[1], address)) { out << "mem.str result=87"; return true; }
        out << "mem.str result=0 \"" << SafeText(address) << "\"";
    } else {
        return false;
    }
    return true;
}

}  // namespace preyvr::dll
