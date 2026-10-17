#pragma once

// The whole-body byte contracts the native lanes check before they call or hook
// an engine function. Each row is {rva, size, hash}: the FNV-1a 64 hash of the
// function's `size` bytes at that RVA.
//
// The tables stay in the *Native.h headers, each with its own row type: several
// of those headers are pinned by receipts, so the shared part is only the check.
#include <cstddef>
#include <cstdint>

namespace preyvr::contract {

inline constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
inline constexpr std::uint64_t kFnvPrime = 1099511628211ull;

// Reads the bytes directly. A caller that can fault keeps its own __try around
// the call; this adds none and removes none.
template <class Row>
bool BodyMatches(std::uintptr_t base, const Row& row)
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(base + row.rva);
    std::uint64_t hash = kFnvOffsetBasis;
    for (std::size_t n = 0; n < row.size; ++n) {
        hash = (hash ^ bytes[n]) * kFnvPrime;
    }
    return hash == row.hash;
}

template <class Table>
bool BodiesMatch(std::uintptr_t base, const Table& table)
{
    for (const auto& row : table) {
        if (!BodyMatches(base, row)) { return false; }
    }
    return true;
}

// Whether `rva` has a row. A function the code calls through a named constant
// must be one its gate checks, or the gate verifies one function while the code
// calls another; static_assert it where the constant is used.
template <class Table>
constexpr bool Gated(const Table& table, std::uintptr_t rva)
{
    for (const auto& row : table) {
        if (row.rva == rva) { return true; }
    }
    return false;
}

} // namespace preyvr::contract
