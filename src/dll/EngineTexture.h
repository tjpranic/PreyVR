#pragma once

// The D3D11 texture behind one of Prey's CTextures, found without trusting a
// pointer that has not shown what it is: CTexture +0x30 is the device texture
// (R-132), and the device texture's +0x00 is the ID3D11Texture2D (seen live on
// the history textures, 2026-10-30). Nothing is called on an object unless its
// vtable is d3d11.dll's. Header-only, so tests that compile a lane's .cpp
// directly need nothing extra.
#include "SafeMemory.h"

#include <windows.h>
#include <d3d11.h>

#include <cctype>
#include <cstdint>
#include <string>

namespace preyvr::dll {

inline constexpr std::size_t kCTextureDevice = 0x30;

// The lower-case file name of the module holding `object`'s first pointer (its
// vtable, for a COM object): "none" when that is no module's, "unreadable" when
// it cannot be read.
inline std::string VtableModule(std::uintptr_t object)
{
    std::uintptr_t vtable = 0;
    if (object == 0 || !SafeRead(object, vtable) || vtable == 0) { return "unreadable"; }
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(vtable), &module) ||
        module == nullptr) {
        return "none";
    }
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) { return "unnamed"; }
    std::string name;
    for (DWORD i = length; i > 0 && path[i - 1] != L'\\' && path[i - 1] != L'/'; --i) {
        const wchar_t c = path[i - 1];
        name.insert(name.begin(), c < 0x80 ? static_cast<char>(std::tolower(static_cast<int>(c))) : '?');
    }
    return name;
}

// 1 when `object` answers ID3D11Texture2D (`desc` filled; `itself` says whether
// the interface is `object` itself), 0 when it does not, -1 when the call
// faulted. Call it only on an object whose vtable is d3d11.dll's.
inline int QueryTexture2D(std::uintptr_t object, D3D11_TEXTURE2D_DESC& desc, bool* itself = nullptr)
{
    __try {
        auto* unknown = reinterpret_cast<IUnknown*>(object);
        ID3D11Texture2D* texture = nullptr;
        if (FAILED(unknown->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture))) ||
            texture == nullptr) {
            return 0;
        }
        texture->GetDesc(&desc);
        if (itself != nullptr) { *itself = reinterpret_cast<std::uintptr_t>(texture) == object; }
        texture->Release();
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// The ID3D11Texture2D a CTexture holds (its device texture's +0x00), checked as
// above and required to be that interface itself so it can be used as one; 0
// otherwise.
inline ID3D11Texture2D* EngineTexture2D(std::uintptr_t ctexture, D3D11_TEXTURE2D_DESC& desc)
{
    std::uintptr_t device = 0, texture = 0;
    if (ctexture == 0 || !SafeRead(ctexture + kCTextureDevice, device) || device == 0 ||
        !SafeRead(device, texture) || texture == 0 || VtableModule(texture) != "d3d11.dll") {
        return nullptr;
    }
    bool itself = false;
    return QueryTexture2D(texture, desc, &itself) == 1 && itself ? reinterpret_cast<ID3D11Texture2D*>(texture)
                                                                  : nullptr;
}

} // namespace preyvr::dll
