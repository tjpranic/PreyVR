#pragma once
#include <windows.h>
#include <d3d11.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <wrl/client.h>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace preyvr::dll {
// Explicit dispatch also lets offline tests exercise the real owner with a
// simulated XR runtime and real D3D11 textures, without starting a VR session.
struct LayerSwapchainApi {
    PFN_xrEnumerateSwapchainFormats formats;
    PFN_xrCreateSwapchain create;
    PFN_xrEnumerateSwapchainImages images;
    PFN_xrAcquireSwapchainImage acquire;
    PFN_xrWaitSwapchainImage wait;
    PFN_xrReleaseSwapchainImage release;
    PFN_xrDestroySwapchain destroy;
};
// The `destroy` every swapchain of the session host uses (XrSessionHost.cpp):
// it also drops the frame that hold frames would submit again, so a hold frame
// never names a destroyed swapchain. XR frame thread, under the session lock.
XrResult XRAPI_CALL DestroyUnheldSwapchain(XrSwapchain swapchain);
// The session's OpenXR calls, with DestroyUnheldSwapchain (XrSessionHost.cpp).
LayerSwapchainApi SessionSwapchainApi();
// The size floor screens copies of the game's own render targets. Layers
// painted from the mod's own pixels are smaller by design and pass their own
// floor to the constructor (the slot card is 1024x224).
inline constexpr UINT kLayerMinWidth=640,kLayerMinHeight=480;
// The largest texture side any layer, capture or panel accepts.
inline constexpr UINT kMaxLayerExtent=8192;
class LayerSwapchain {
public:
    enum class Upload { ready, failed, sessionFault };
    // Which step the last Prepare refused at, so a layer that cannot be created
    // names the step (and the runtime's XrResult) instead of one reason for all.
    enum class Refusal { none, session, source, presentationFormat, enumerateFormats, formatNotListed,
                         create, images, image };
    static const char* RefusalName(Refusal refusal);
    explicit LayerSwapchain(LayerSwapchainApi api,UINT minWidth=kLayerMinWidth,
                            UINT minHeight=kLayerMinHeight)
        :api_(api),minWidth_(minWidth),minHeight_(minHeight){}
    ~LayerSwapchain(){Reset();}
    LayerSwapchain(const LayerSwapchain&)=delete;
    LayerSwapchain& operator=(const LayerSwapchain&)=delete;
    // Match the active main/menu swapchain's interpretation of copied pixels.
    // The resource's UNORM spelling alone does not establish its encoding.
    bool Prepare(XrSession session, const D3D11_TEXTURE2D_DESC& source, DXGI_FORMAT presentationFormat);
    Upload Copy(ID3D11DeviceContext* context, ID3D11Texture2D* source);
    void Reset(); // Must run before destroying the owning XR session.
    XrSwapchain Handle() const {return chain_;}
    const D3D11_TEXTURE2D_DESC& Description() const {return desc_;}
    Refusal LastRefusal() const {return refusal_;}
    XrResult LastResult() const {return result_;}
private:
    LayerSwapchainApi api_;
    UINT minWidth_,minHeight_;
    XrSession session_=XR_NULL_HANDLE;
    XrSwapchain chain_=XR_NULL_HANDLE;
    D3D11_TEXTURE2D_DESC desc_{};
    DXGI_FORMAT presentationFormat_=DXGI_FORMAT_UNKNOWN;
    std::vector<XrSwapchainImageD3D11KHR> images_;
    bool attempted_=false, fault_=false;
    Refusal refusal_=Refusal::none;
    XrResult result_=XR_SUCCESS;
};
// A LayerSwapchain of the session (SessionSwapchainApi), created on first use.
std::unique_ptr<LayerSwapchain> SessionLayerSwapchain(UINT minWidth=kLayerMinWidth,UINT minHeight=kLayerMinHeight);
// **The first of `preferred` the runtime lists**, or DXGI_FORMAT_UNKNOWN, also
// when the list cannot be read or holds none or 256 or more formats.
DXGI_FORMAT FirstListedFormat(PFN_xrEnumerateSwapchainFormats formats,XrSession session,
                              std::initializer_list<DXGI_FORMAT> preferred);
// **One swapchain image, from acquire to release**, for the swapchains written
// whole: the session's own and the static layers (LayerSwapchain::Copy keeps
// its own, with a bounded wait and a fault latch). Acquired and waited for,
// without a time limit, when constructed. A failed wait releases nothing --
// the runtime never handed the image over -- and every caller treats it as a
// session fault. Release gives the image back, after a wait only, and says
// whether the runtime took it; what a refusal means is the caller's.
class AcquiredImage {
public:
    AcquiredImage()=default; // nothing acquired, until Acquire
    AcquiredImage(const LayerSwapchainApi& api,XrSwapchain swapchain){Acquire(api,swapchain);}
    AcquiredImage(const AcquiredImage&)=delete;
    AcquiredImage& operator=(const AcquiredImage&)=delete;
    bool Acquired() const {return acquired_;}
    bool Waited() const {return waited_;}
    bool WaitFailed() const {return acquired_&&!waited_;}
    uint32_t Index() const {return index_;}
    // Once, on an image not yet acquired.
    void Acquire(const LayerSwapchainApi& api,XrSwapchain swapchain);
    bool Release();
private:
    PFN_xrReleaseSwapchainImage release_=nullptr;
    XrSwapchain swapchain_=XR_NULL_HANDLE;
    uint32_t index_=0;
    bool acquired_=false,waited_=false;
};
// **A static-image layer** (the laser's texture, the guide card): a swapchain
// created with XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT, its one image filled once
// from `pixels`, rows of width*4 bytes in `format`'s channel order. `swapchain`
// holds the handle once created, whatever follows; the session destroys it.
// `waited`: the image was handed over (and given back); `ready`: it holds the
// pixels and the runtime took it back; `waitFailed`: a session fault.
struct StaticUpload { bool waited=false,ready=false,waitFailed=false; };
StaticUpload CreateStaticLayer(const LayerSwapchainApi& api,XrSession session,ID3D11Device* device,int64_t format,
                               uint32_t width,uint32_t height,const void* pixels,XrSwapchain& swapchain);
// **A layer of the mod's own pixels** (the slot card, the head-in-wall fade): a
// `width` x `height` texture in the first sRGB format the runtime lists
// (B8G8R8A8, else R8G8B8A8), and a LayerSwapchain whose size floor is that
// size. Made on the first Ensure and never retried, its outcome logged as
// `detail=<detail>`. The layer shows the last image Paint uploaded.
class PaintedLayer {
public:
    PaintedLayer(const char* detail,UINT width,UINT height):detail_(detail),width_(width),height_(height){}
    // Makes the texture and swapchain on the first call. True once both exist.
    bool Ensure(const LayerSwapchainApi& api,XrSession session,ID3D11Device* device,void (*log)(const std::string&));
    bool Ready() const {return swapchain_!=nullptr;}
    DXGI_FORMAT Format() const {return format_;}
    XrSwapchain Handle() const {return swapchain_?swapchain_->Handle():XR_NULL_HANDLE;}
    // `pixels`: rows of width*4 bytes in Format()'s channel order. Ready() only.
    LayerSwapchain::Upload Paint(ID3D11DeviceContext* context,const void* pixels);
    void Reset(); // the swapchain, then the texture; before the session goes
private:
    const char* detail_;
    UINT width_,height_;
    bool attempted_=false;
    DXGI_FORMAT format_=DXGI_FORMAT_UNKNOWN;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
    std::unique_ptr<LayerSwapchain> swapchain_;
};
}
