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
#include <string_view>
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

inline bool SafeCopy(void* out, std::uintptr_t from, std::size_t size)
{
    return SafeCopy(out, reinterpret_cast<const void*>(from), size);
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

// The T `offset` bytes past `base`.
template <class T>
bool SafeReadAt(const void* base, std::size_t offset, T& out)
{
    return SafeCopy(&out, static_cast<const std::uint8_t*>(base) + offset, sizeof(out));
}

// Whether the bytes at `address` are `hex` (two lower-case digits a byte, at
// most 0x80 bytes). False when they differ or cannot be read.
inline bool SafeEqualHex(std::uintptr_t address, std::string_view hex)
{
    std::uint8_t bytes[0x80]{};
    const std::size_t size = hex.size() / 2;
    if (size == 0 || size > sizeof(bytes) || !SafeCopy(bytes, reinterpret_cast<const void*>(address), size)) {
        return false;
    }
    const auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (std::size_t i = 0; i < size; ++i) {
        if (bytes[i] != static_cast<std::uint8_t>((nibble(hex[2 * i]) << 4) | nibble(hex[2 * i + 1]))) {
            return false;
        }
    }
    return true;
}

// Copies the printable C string at `source`, cut to fit `out`; false for a null,
// empty or unreadable one, or one with anything unprintable in what fits.
inline bool SafeCopyPrintable(const char* source, char* out, std::size_t size)
{
    __try {
        if (!source) { return false; }
        std::size_t n = 0;
        for (; n + 1 < size && source[n]; ++n) {
            const unsigned char c = static_cast<unsigned char>(source[n]);
            if (c < 32 || c > 126) { return false; }
            out[n] = static_cast<char>(c);
        }
        out[n] = 0;
        return n > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
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
