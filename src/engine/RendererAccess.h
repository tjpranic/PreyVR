#pragma once

#include "engine/EngineMap.h"

#include <windows.h>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <cstdint>

// Prey's renderer's own D3D11 objects, read from the renderer
// (engine::RendererLayout): the device it created at startup (R-007) and its
// swap chain. Kept out of EngineAccess.h, which the lanes include without D3D.
namespace preyvr::dll {

inline ID3D11Device* RendererDevice(std::uintptr_t renderer)
{
    return renderer != 0 ? *reinterpret_cast<ID3D11Device* const*>(renderer + engine::RendererLayout::device)
                         : nullptr;
}
inline ID3D11Device* RendererDevice(const void* renderer)
{
    return RendererDevice(reinterpret_cast<std::uintptr_t>(renderer));
}

inline IDXGISwapChain* RendererSwapChain(std::uintptr_t renderer)
{
    return renderer != 0 ? *reinterpret_cast<IDXGISwapChain* const*>(renderer + engine::RendererLayout::swapchain)
                         : nullptr;
}
inline IDXGISwapChain* RendererSwapChain(const void* renderer)
{
    return RendererSwapChain(reinterpret_cast<std::uintptr_t>(renderer));
}

// The swap chain's buffer 0, the image Prey presents, or null.
inline Microsoft::WRL::ComPtr<ID3D11Texture2D> AcquireBackBuffer(IDXGISwapChain* swapChain)
{
    Microsoft::WRL::ComPtr<ID3D11Texture2D> buffer;
    ID3D11Texture2D* image = nullptr;
    if (swapChain != nullptr &&
        SUCCEEDED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&image)))) {
        buffer.Attach(image);
    }
    return buffer;
}
inline Microsoft::WRL::ComPtr<ID3D11Texture2D> AcquireBackBuffer(const void* renderer)
{
    return AcquireBackBuffer(RendererSwapChain(renderer));
}

// The device's immediate context, as a reference this frame holds.
inline Microsoft::WRL::ComPtr<ID3D11DeviceContext> ImmediateContext(ID3D11Device* device)
{
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    if (device != nullptr) {
        device->GetImmediateContext(&context);
    }
    return context;
}

} // namespace preyvr::dll
