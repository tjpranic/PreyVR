# Public headers

These are the headers for the pure logic in `src/common` (built into `preyvr_core`), plus the engine contract tables the DLL gates on.

- **Pure APIs:** they keep D3D11/OpenXR handles and live game pointers out of their data and math, so each one can be tested without the game. Keep it that way.
- **`EngineMap.h`:** the landmarks the bootstrap validates at startup, and the engine layouts several modules share.
- **`*Native.h`:** one feature's RVAs, vtable slots and FNV body hashes. Each is the contract that feature checks (`NativeContract.h`) before it hooks or calls anything. The `tools/re/verify_*.py` scripts check these tables against PreyDll.dll, and several of them parse the `{0x..,n,0x..ull}, // Name` rows, so keep that row format.
- **Receipt-pinned headers:** do not edit `EquipmentNative.h`, `NativeWristNative.h`, `PsiMedkitNative.h` or `PsychoscopeNative.h`. Receipts in `receipts/` pin their path and bytes.
