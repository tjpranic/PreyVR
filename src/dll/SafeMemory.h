#pragma once

// Guarded access to engine memory: a fault returns false instead of taking the
// game down. Structured exception handling, not a VirtualQuery per access: it
// costs nothing when nothing faults, and these run on render and animation
// threads, some of them hundreds of times a frame.
//
// Each primitive guards one copy or compare. A read of several fields that must
// succeed or fail together keeps its own single __try.
//
// Header-only, with no preyvr/ includes, because some tests compile a lane's
// .cpp directly.
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>

namespace preyvr::dll {

inline bool SafeCopy(void* out, const void* from, std::size_t size)
{
    __try {
        std::memcpy(out, from, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// False when the bytes differ or cannot be read.
inline bool SafeEqual(const void* at, const void* expected, std::size_t size)
{
    __try {
        return std::memcmp(at, expected, size) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <class T>
bool SafeRead(const void* at, T& out)
{
    static_assert(std::is_trivially_copyable_v<T>);
    return SafeCopy(&out, at, sizeof(T));
}

template <class T>
bool SafeRead(std::uintptr_t at, T& out)
{
    return SafeRead(reinterpret_cast<const void*>(at), out);
}

// The text at `address`: up to 96 characters, to its terminator or the first
// byte that cannot be read, with anything unprintable as '?'.
inline std::string SafeText(std::uint64_t address)
{
    std::string text;
    for (unsigned i = 0; i < 96 && address != 0; ++i) {
        char c = 0;
        if (!SafeCopy(&c, reinterpret_cast<const void*>(address + i), 1) || c == 0) { break; }
        text.push_back(c >= 32 && c < 127 ? c : '?');
    }
    return text;
}

// Committed and executable, and inside `module` when one is given. A query, not
// a read, so it is safe on an address nothing vouches for -- but a syscall, so
// for install-time gates rather than per-frame reads.
inline bool IsExecutableAddress(const void* address, HMODULE module = nullptr)
{
    MEMORY_BASIC_INFORMATION info{};
    return VirtualQuery(address, &info, sizeof(info)) == sizeof(info) && info.State == MEM_COMMIT &&
           (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0 &&
           (module == nullptr || info.AllocationBase == module);
}

} // namespace preyvr::dll
