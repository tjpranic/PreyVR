#pragma once

// The gate a native lane checks before it calls or hooks the engine: the bodies
// its *Native.h table pins (NativeContract.h), the vtable slots that reach them,
// and PreyDll's base once both pass. Each lane keeps its own GateBodies, its own
// gResolve seam for the tests and its own install-once block; these are the
// parts every one of them wrote the same way.
//
// Header-only, so the tests that compile a lane's .cpp directly need nothing
// extra.
#include "engine/EngineAccess.h"
#include "engine/NativeContract.h"
#include "engine/SafeMemory.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace preyvr::dll {

// contract::BodiesMatch, with a body that faults to read as no match.
template <class Table>
bool BodiesMatchGuarded(std::uintptr_t base, const Table& table)
{
    __try {
        return contract::BodiesMatch(base, table);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Whether the vtable at `table` holds the function at `rva` at byte offset
// `slot`. Both are RVAs into the module at `base`; a slot that cannot be read
// does not match.
inline bool SlotIs(std::uintptr_t base, std::uintptr_t table, std::size_t slot, std::uintptr_t rva)
{
    std::uintptr_t value = 0;
    return SafeRead(base + table + slot, value) && value == base + rva;
}

// PreyDll's base, once it is loaded and `Gate` passes on it: ERROR_NOT_READY
// before, ERROR_BAD_EXE_FORMAT (and 0) for other bytes. A lane points its
// gResolve here.
template <bool (*Gate)(std::uintptr_t)>
DWORD ResolveBase(std::uintptr_t& base)
{
    base = PreyBase();
    if (base == 0) { return ERROR_NOT_READY; }
    if (!Gate(base)) {
        base = 0;
        return ERROR_BAD_EXE_FORMAT;
    }
    return ERROR_SUCCESS;
}

} // namespace preyvr::dll
