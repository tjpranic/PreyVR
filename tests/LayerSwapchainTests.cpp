#include "LayerSwapchain.h"
#include "XrPose.h"
#include <wrl/client.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace preyvr::dll;
#define CHECK(x) do { if(!(x)) throw std::runtime_error(#x); } while(false)
namespace {
struct Runtime {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> image;
    XrSwapchainCreateInfo created{};
    std::vector<std::string> calls;
    std::vector<int64_t> formats{DXGI_FORMAT_R8G8B8A8_UNORM_SRGB};
    XrResult createResult=XR_SUCCESS,acquireResult=XR_SUCCESS,waitResult=XR_SUCCESS,releaseResult=XR_SUCCESS;
    XrResult formatsResult=XR_SUCCESS;
    // The static layers and the session's own swapchain wait without a limit;
    // LayerSwapchain::Copy never does.
    bool unboundedWait=false;
    XrDuration lastTimeout=0;
    std::vector<std::string> lines;
    uint32_t index=0;
    bool nullImage=false;
    unsigned serial=0;
    XrSwapchain handle=XR_NULL_HANDLE;
} r;
XrSession session=reinterpret_cast<XrSession>(0x1000);
D3D11_TEXTURE2D_DESC Description(unsigned width=640) {
    D3D11_TEXTURE2D_DESC d{};
    d.Width=width;d.Height=480;d.ArraySize=1;d.MipLevels=1;
    d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;
    d.BindFlags=D3D11_BIND_RENDER_TARGET;return d;
}
ComPtr<ID3D11Texture2D> Texture(const D3D11_TEXTURE2D_DESC& d,uint32_t pixel) {
    std::vector<uint32_t> pixels(static_cast<size_t>(d.Width)*d.Height,pixel);
    D3D11_SUBRESOURCE_DATA data{pixels.data(),d.Width*4,0};
    ComPtr<ID3D11Texture2D> t;CHECK(SUCCEEDED(r.device->CreateTexture2D(&d,&data,&t)));return t;
}
void CheckPixels(ID3D11Texture2D* texture,uint32_t pixel) {
    D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
    d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;CHECK(SUCCEEDED(r.device->CreateTexture2D(&d,nullptr,&staging)));
    r.context->CopyResource(staging.Get(),texture);
    D3D11_MAPPED_SUBRESOURCE map{};CHECK(SUCCEEDED(r.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)));
    bool matches=true;
    for(unsigned y=0;y<d.Height;++y) {
        const auto row=reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(map.pData)+y*map.RowPitch);
        matches=matches && std::all_of(row,row+d.Width,[&](auto v){return v==pixel;});
    }
    r.context->Unmap(staging.Get(),0);CHECK(matches);
}
XrResult XRAPI_CALL Formats(XrSession s,uint32_t capacity,uint32_t* count,int64_t* out) {
    CHECK(s!=XR_NULL_HANDLE);*count=static_cast<uint32_t>(r.formats.size());
    if(capacity) {CHECK(capacity>=*count);std::copy(r.formats.begin(),r.formats.end(),out);}
    return r.formatsResult;
}
XrResult XRAPI_CALL Create(XrSession,const XrSwapchainCreateInfo* info,XrSwapchain* out) {
    r.calls.push_back("create");r.created=*info;
    if(r.createResult!=XR_SUCCESS)return r.createResult;
    auto d=Description(info->width);d.Height=info->height;d.Format=static_cast<DXGI_FORMAT>(info->format);
    r.image=Texture(d,0x12345678);
    r.handle=reinterpret_cast<XrSwapchain>(static_cast<uintptr_t>(0x2000+(++r.serial)*0x10));
    *out=r.handle;return XR_SUCCESS;
}
XrResult XRAPI_CALL Images(XrSwapchain s,uint32_t capacity,uint32_t* count,XrSwapchainImageBaseHeader* out) {
    CHECK(s==r.handle);*count=1;
    if(capacity)reinterpret_cast<XrSwapchainImageD3D11KHR*>(out)->texture=r.nullImage?nullptr:r.image.Get();
    return XR_SUCCESS;
}
XrResult XRAPI_CALL Acquire(XrSwapchain s,const XrSwapchainImageAcquireInfo*,uint32_t* index) {
    CHECK(s==r.handle);r.calls.push_back("acquire");*index=r.index;return r.acquireResult;
}
XrResult XRAPI_CALL Wait(XrSwapchain s,const XrSwapchainImageWaitInfo* info) {
    CHECK(s==r.handle);r.lastTimeout=info->timeout;
    CHECK(r.unboundedWait?info->timeout==XR_INFINITE_DURATION:info->timeout>0 && info->timeout<=50000000);
    r.calls.push_back("wait");return r.waitResult;
}
XrResult XRAPI_CALL Release(XrSwapchain s,const XrSwapchainImageReleaseInfo*) {
    CHECK(s==r.handle);r.calls.push_back("release");return r.releaseResult;
}
XrResult XRAPI_CALL Destroy(XrSwapchain s) {
    CHECK(s==r.handle);r.calls.push_back("destroy");r.handle=XR_NULL_HANDLE;r.image.Reset();return XR_SUCCESS;
}
LayerSwapchainApi Api(){return {Formats,Create,Images,Acquire,Wait,Release,Destroy};}
void Calls(std::initializer_list<const char*> expected) {
    std::vector<std::string> e(expected.begin(),expected.end());CHECK(r.calls==e);r.calls.clear();
}
void LifecycleAndPixels() {
    LayerSwapchain owner(Api());auto d=Description();
    CHECK(owner.Prepare(session,d,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));auto first=owner.Handle();
    CHECK(r.created.arraySize==1 && r.created.faceCount==1 && r.created.mipCount==1 && r.created.sampleCount==1);
    CHECK(r.created.format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    CHECK(owner.Prepare(session,d,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));Calls({"create"});
    auto source=Texture(d,0x80402010),world=Texture(d,0xFF13579B);
    CHECK(owner.Copy(r.context.Get(),source.Get())==LayerSwapchain::Upload::ready);
    Calls({"acquire","wait","release"});
    CheckPixels(r.image.Get(),0x80402010);CheckPixels(world.Get(),0xFF13579B);
    CHECK(owner.Prepare(session,Description(800),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));CHECK(owner.Handle()!=first);Calls({"destroy","create"});
    CHECK(owner.Copy(r.context.Get(),source.Get())==LayerSwapchain::Upload::failed);Calls({});
    CHECK(owner.Prepare(reinterpret_cast<XrSession>(0x1100),d,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));Calls({"destroy","create"});
    CHECK(owner.Copy(nullptr,source.Get())==LayerSwapchain::Upload::failed);Calls({});
    owner.Reset();CHECK(!owner.Handle());Calls({"destroy"});
    owner.Reset();Calls({});
}
void Refusals() {
    LayerSwapchain owner(Api());auto d=Description();
    d.ArraySize=2;CHECK(!owner.Prepare(session,d,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));Calls({});
    CHECK(owner.LastRefusal()==LayerSwapchain::Refusal::source);
    d=Description();d.SampleDesc.Count=4;CHECK(!owner.Prepare(session,d,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));Calls({});
    CHECK(owner.LastRefusal()==LayerSwapchain::Refusal::source);
    d=Description();d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;CHECK(!owner.Prepare(session,d,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));Calls({});
    CHECK(owner.LastRefusal()==LayerSwapchain::Refusal::presentationFormat);
    d=Description();r.nullImage=true;
    CHECK(!owner.Prepare(session,d,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));CHECK(!owner.Handle());Calls({"create","destroy"});
    CHECK(owner.LastRefusal()==LayerSwapchain::Refusal::image);
    CHECK(!owner.Prepare(session,d,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));Calls({}); // no allocation loop on refusal
    CHECK(owner.LastRefusal()==LayerSwapchain::Refusal::image);
    r.nullImage=false;owner.Reset();CHECK(owner.Prepare(session,d,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));Calls({"create"});
    CHECK(owner.LastRefusal()==LayerSwapchain::Refusal::none);
    auto source=Texture(d,0xCC664422);
    r.acquireResult=XR_ERROR_RUNTIME_FAILURE;
    CHECK(owner.Copy(r.context.Get(),source.Get())==LayerSwapchain::Upload::failed);Calls({"acquire"});
    CheckPixels(r.image.Get(),0x12345678);r.acquireResult=XR_SUCCESS;
    r.index=2;
    CHECK(owner.Copy(r.context.Get(),source.Get())==LayerSwapchain::Upload::failed);
    Calls({"acquire","wait","release"});CheckPixels(r.image.Get(),0x12345678);r.index=0;
    // A positive timeout is not a waited image: no copy, no release, no retry.
    r.waitResult=XR_TIMEOUT_EXPIRED;
    CHECK(owner.Copy(r.context.Get(),source.Get())==LayerSwapchain::Upload::sessionFault);
    Calls({"acquire","wait"});CheckPixels(r.image.Get(),0x12345678);
    CHECK(owner.Copy(r.context.Get(),source.Get())==LayerSwapchain::Upload::sessionFault);Calls({});
    CHECK(!owner.Prepare(session,d,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));Calls({});
    owner.Reset();Calls({"destroy"});r.waitResult=XR_SUCCESS;
    CHECK(owner.Prepare(session,d,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));Calls({"create"});r.releaseResult=XR_ERROR_RUNTIME_FAILURE;
    CHECK(owner.Copy(r.context.Get(),source.Get())==LayerSwapchain::Upload::sessionFault);
    Calls({"acquire","wait","release"});
    CHECK(owner.Copy(r.context.Get(),source.Get())==LayerSwapchain::Upload::sessionFault);Calls({});
    r.releaseResult=XR_SUCCESS;
}
void PresentationPolicy() {
    // Source resource spelling and runtime preference order do not decide the
    // copied image's interpretation: the active main/menu format does.
    r.formats={DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_R8G8B8A8_UNORM,
               DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,DXGI_FORMAT_B8G8R8A8_UNORM};
    LayerSwapchain owner(Api());
    for(const auto sourceFormat:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM}) {
        auto d=Description();d.Format=sourceFormat;
        const auto srgb=sourceFormat==DXGI_FORMAT_R8G8B8A8_UNORM?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        CHECK(owner.Prepare(session,d,sourceFormat));CHECK(r.created.format==sourceFormat);Calls({"create"});
        auto source=Texture(d,0x80402010);
        CHECK(owner.Copy(r.context.Get(),source.Get())==LayerSwapchain::Upload::ready);
        CheckPixels(r.image.Get(),0x80402010);Calls({"acquire","wait","release"});
        CHECK(owner.Prepare(session,d,srgb));CHECK(r.created.format==srgb);Calls({"destroy","create"});
        CHECK(owner.Prepare(session,d,srgb));Calls({});
        CHECK(owner.Copy(r.context.Get(),source.Get())==LayerSwapchain::Upload::ready);
        CheckPixels(r.image.Get(),0x80402010);Calls({"acquire","wait","release"});
        owner.Reset();Calls({"destroy"});
    }
    // Do not silently change transfer function if the requested one is absent.
    r.formats={DXGI_FORMAT_R8G8B8A8_UNORM_SRGB};
    CHECK(!owner.Prepare(session,Description(),DXGI_FORMAT_R8G8B8A8_UNORM));Calls({});
    CHECK(owner.LastRefusal()==LayerSwapchain::Refusal::formatNotListed);
    CHECK(!owner.Prepare(session,Description(),DXGI_FORMAT_R8G8B8A8_TYPELESS));Calls({});
    CHECK(owner.LastRefusal()==LayerSwapchain::Refusal::presentationFormat);
    CHECK(!owner.Prepare(session,Description(),DXGI_FORMAT_B8G8R8A8_UNORM_SRGB));Calls({});
    CHECK(owner.LastRefusal()==LayerSwapchain::Refusal::presentationFormat);
}
void HudShapeAndRefusalReasons() {
    // The HUD's shape: a capture spelled with its render target's sRGB view,
    // presented in the main swapchain's linear format, on a runtime that lists
    // only that one. The capture's own spelling is never what is asked for.
    r.formats={DXGI_FORMAT_R8G8B8A8_UNORM};
    LayerSwapchain owner(Api());auto d=Description();d.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    CHECK(owner.Prepare(session,d,DXGI_FORMAT_R8G8B8A8_UNORM));CHECK(r.created.format==DXGI_FORMAT_R8G8B8A8_UNORM);
    CHECK(owner.LastRefusal()==LayerSwapchain::Refusal::none);Calls({"create"});
    auto source=Texture(d,0x80402010);
    CHECK(owner.Copy(r.context.Get(),source.Get())==LayerSwapchain::Upload::ready);
    CheckPixels(r.image.Get(),0x80402010);Calls({"acquire","wait","release"});
    owner.Reset();Calls({"destroy"});
    // A runtime that refuses creation is reported with its own XrResult.
    r.createResult=XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
    CHECK(!owner.Prepare(session,d,DXGI_FORMAT_R8G8B8A8_UNORM));CHECK(!owner.Handle());Calls({"create"});
    CHECK(owner.LastRefusal()==LayerSwapchain::Refusal::create);
    CHECK(owner.LastResult()==XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED);
    CHECK(std::string(LayerSwapchain::RefusalName(owner.LastRefusal()))=="create_swapchain");
    CHECK(!owner.Prepare(session,d,DXGI_FORMAT_R8G8B8A8_UNORM));Calls({}); // cached; reason kept
    CHECK(owner.LastResult()==XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED);
    r.createResult=XR_SUCCESS;owner.Reset();Calls({});
    CHECK(owner.LastRefusal()==LayerSwapchain::Refusal::none && owner.LastResult()==XR_SUCCESS);
    CHECK(!owner.Prepare(XR_NULL_HANDLE,d,DXGI_FORMAT_R8G8B8A8_UNORM));Calls({});
    CHECK(owner.LastRefusal()==LayerSwapchain::Refusal::session);
}
void SizeFloor() {
    // The slot card (1024x224) is shorter than the inventory floor: refused by
    // default, created and copied with its own floor. Captures of the game's
    // targets keep the inventory floor.
    r.formats={DXGI_FORMAT_R8G8B8A8_UNORM_SRGB};
    auto d=Description(1024);d.Height=224;
    CHECK(!InventoryTextureCompatible(d,d));
    LayerSwapchain inventory(Api());
    CHECK(!inventory.Prepare(session,d,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));Calls({});
    CHECK(inventory.LastRefusal()==LayerSwapchain::Refusal::source);
    LayerSwapchain card(Api(),1024,224);
    CHECK(card.Prepare(session,d,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));Calls({"create"});
    auto source=Texture(d,0x80402010);
    CHECK(card.Copy(r.context.Get(),source.Get())==LayerSwapchain::Upload::ready);
    CheckPixels(r.image.Get(),0x80402010);Calls({"acquire","wait","release"});
    card.Reset();Calls({"destroy"});
}
void Formats() {
    // The caller's preference decides, not the runtime's order; unread or
    // implausible lists give nothing.
    const auto api=Api();
    r.formats={DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB};
    CHECK(FirstListedFormat(api.formats,session,{DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB})==
          DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
    CHECK(FirstListedFormat(api.formats,session,{DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB})==
          DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    r.formats={DXGI_FORMAT_R8G8B8A8_UNORM};
    CHECK(FirstListedFormat(api.formats,session,{DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB})==
          DXGI_FORMAT_UNKNOWN);
    r.formats={};
    CHECK(FirstListedFormat(api.formats,session,{DXGI_FORMAT_R8G8B8A8_UNORM})==DXGI_FORMAT_UNKNOWN);
    r.formats.assign(255,DXGI_FORMAT_R8G8B8A8_UNORM);r.formats.back()=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    CHECK(FirstListedFormat(api.formats,session,{DXGI_FORMAT_B8G8R8A8_UNORM_SRGB})==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
    r.formats.push_back(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    CHECK(FirstListedFormat(api.formats,session,{DXGI_FORMAT_B8G8R8A8_UNORM_SRGB})==DXGI_FORMAT_UNKNOWN);
    r.formats={DXGI_FORMAT_R8G8B8A8_UNORM_SRGB};r.formatsResult=XR_ERROR_RUNTIME_FAILURE;
    CHECK(FirstListedFormat(api.formats,session,{DXGI_FORMAT_R8G8B8A8_UNORM_SRGB})==DXGI_FORMAT_UNKNOWN);
    r.formatsResult=XR_SUCCESS;Calls({});
}
void Acquired() {
    // Acquire, an unbounded wait, and one release only after a wait.
    const auto api=Api();r.unboundedWait=true;
    XrSwapchain chain=XR_NULL_HANDLE;XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};info.width=640;info.height=480;
    info.format=DXGI_FORMAT_R8G8B8A8_UNORM;CHECK(api.create(session,&info,&chain)==XR_SUCCESS);Calls({"create"});
    {AcquiredImage none;CHECK(!none.Acquired() && !none.Waited() && !none.WaitFailed() && !none.Release());Calls({});}
    {r.index=1;AcquiredImage image(api,chain);Calls({"acquire","wait"});
     CHECK(image.Acquired() && image.Waited() && !image.WaitFailed() && image.Index()==1);
     CHECK(r.lastTimeout==XR_INFINITE_DURATION);
     CHECK(image.Release());Calls({"release"});CHECK(!image.Release());Calls({});r.index=0;}
    {r.acquireResult=XR_ERROR_RUNTIME_FAILURE;AcquiredImage image(api,chain);Calls({"acquire"});
     CHECK(!image.Acquired() && !image.WaitFailed() && !image.Release());Calls({});r.acquireResult=XR_SUCCESS;}
    {r.waitResult=XR_ERROR_RUNTIME_FAILURE;AcquiredImage image(api,chain);Calls({"acquire","wait"});
     CHECK(image.WaitFailed() && !image.Waited() && !image.Release());Calls({});r.waitResult=XR_SUCCESS;}
    {AcquiredImage image;image.Acquire(api,chain);Calls({"acquire","wait"});r.releaseResult=XR_ERROR_RUNTIME_FAILURE;
     CHECK(!image.Release());Calls({"release"});r.releaseResult=XR_SUCCESS;}
    api.destroy(chain);Calls({"destroy"});r.unboundedWait=false;
}
void StaticLayers() {
    // Created static, filled once, given back; each failure stops where it is.
    const auto api=Api();r.unboundedWait=true;
    std::vector<uint32_t> pixels(64*32,0x80FF2010);
    XrSwapchain chain=XR_NULL_HANDLE;
    auto upload=CreateStaticLayer(api,session,r.device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,64,32,pixels.data(),chain);
    CHECK(upload.waited && upload.ready && !upload.waitFailed && chain==r.handle);
    Calls({"create","acquire","wait","release"});
    CHECK(r.created.createFlags==XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT && r.created.width==64 && r.created.height==32);
    CHECK(r.created.usageFlags==(XR_SWAPCHAIN_USAGE_SAMPLED_BIT|XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT));
    CHECK(r.created.arraySize==1 && r.created.mipCount==1 && r.created.sampleCount==1 && r.created.faceCount==1);
    CheckPixels(r.image.Get(),0x80FF2010);api.destroy(chain);Calls({"destroy"});
    r.createResult=XR_ERROR_RUNTIME_FAILURE;
    upload=CreateStaticLayer(api,session,r.device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,64,32,pixels.data(),chain);
    CHECK(!upload.waited && !upload.ready && !upload.waitFailed);Calls({"create"});r.createResult=XR_SUCCESS;
    r.waitResult=XR_ERROR_RUNTIME_FAILURE;
    upload=CreateStaticLayer(api,session,r.device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,64,32,pixels.data(),chain);
    CHECK(upload.waitFailed && !upload.waited && !upload.ready);Calls({"create","acquire","wait"});
    r.waitResult=XR_SUCCESS;api.destroy(chain);Calls({"destroy"});
    r.releaseResult=XR_ERROR_RUNTIME_FAILURE;
    upload=CreateStaticLayer(api,session,r.device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,64,32,pixels.data(),chain);
    CHECK(upload.waited && !upload.ready);Calls({"create","acquire","wait","release"});
    r.releaseResult=XR_SUCCESS;api.destroy(chain);Calls({"destroy"});
    r.index=2;
    upload=CreateStaticLayer(api,session,r.device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,64,32,pixels.data(),chain);
    CHECK(upload.waited && !upload.ready);Calls({"create","acquire","wait","release"});CheckPixels(r.image.Get(),0x12345678);
    r.index=0;api.destroy(chain);Calls({"destroy"});r.unboundedWait=false;
}
void LogLine(const std::string& line){r.lines.push_back(line);}
void PaintedLayers() {
    // Made once with its own size as the floor; the outcome logged as the
    // slot card's and the fade's lines were.
    const auto api=Api();
    r.formats={DXGI_FORMAT_R8G8B8A8_UNORM_SRGB};
    PaintedLayer card("test_swapchain",1024,224);
    CHECK(!card.Ready() && card.Handle()==XR_NULL_HANDLE);
    CHECK(card.Ensure(api,session,r.device.Get(),LogLine) && card.Ready() && card.Handle()==r.handle);
    CHECK(card.Format()==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);Calls({"create"});
    CHECK(r.lines==std::vector<std::string>{"result=0 detail=test_swapchain format=29"});r.lines.clear();
    CHECK(card.Ensure(api,session,r.device.Get(),LogLine));Calls({});CHECK(r.lines.empty());
    std::vector<uint32_t> pixels(1024*224,0xFF102030);
    CHECK(card.Paint(r.context.Get(),pixels.data())==LayerSwapchain::Upload::ready);Calls({"acquire","wait","release"});
    CheckPixels(r.image.Get(),0xFF102030);
    card.Reset();Calls({"destroy"});CHECK(!card.Ready());
    r.formats={DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB};
    CHECK(card.Ensure(api,session,r.device.Get(),LogLine) && card.Format()==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
    Calls({"create"});r.lines.clear();card.Reset();Calls({"destroy"});
    // No sRGB format: refused once, never retried.
    r.formats={DXGI_FORMAT_R8G8B8A8_UNORM};
    PaintedLayer fade("fade_swapchain",640,480);
    CHECK(!fade.Ensure(api,session,r.device.Get(),LogLine));Calls({});
    CHECK(r.lines==std::vector<std::string>{"result=refused detail=fade_swapchain reason=no_srgb_format"});r.lines.clear();
    r.formats={DXGI_FORMAT_R8G8B8A8_UNORM_SRGB};
    CHECK(!fade.Ensure(api,session,r.device.Get(),LogLine));Calls({});CHECK(r.lines.empty());
    // A refused swapchain names the step and the runtime's answer, and is dropped.
    fade.Reset();r.createResult=XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
    CHECK(!fade.Ensure(api,session,r.device.Get(),LogLine) && !fade.Ready());Calls({"create"});
    CHECK(r.lines==std::vector<std::string>{"result=refused detail=fade_swapchain reason=create_swapchain xrResult=" +
                                            std::to_string(XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED)+" format=29"});
    r.lines.clear();r.createResult=XR_SUCCESS;
}
void Poses() {
    // Field for field, both ways; and the quad both eyes see.
    const preyvr::Pose pose{{0.1f,-0.2f,0.3f,0.9273618f},{1.5f,-2.25f,3.125f}};
    const XrPosef xr=ToXrPose(pose);
    CHECK(xr.orientation.x==0.1f && xr.orientation.y==-0.2f && xr.orientation.z==0.3f && xr.orientation.w==0.9273618f);
    CHECK(xr.position.x==1.5f && xr.position.y==-2.25f && xr.position.z==3.125f);
    const preyvr::Pose back=FromXrPose(xr);
    CHECK(back.orientation.x==pose.orientation.x && back.orientation.w==pose.orientation.w &&
          back.position.y==pose.position.y && back.position.z==pose.position.z);
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    const auto space=reinterpret_cast<XrSpace>(0x3000);const auto chain=reinterpret_cast<XrSwapchain>(0x3100);
    FillQuad(quad,space,pose,{0.5f,0.25f},chain,{{2,3},{40,50}},XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT);
    CHECK(quad.type==XR_TYPE_COMPOSITION_LAYER_QUAD && quad.space==space && quad.eyeVisibility==XR_EYE_VISIBILITY_BOTH);
    CHECK(quad.layerFlags==XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT && quad.size.width==0.5f && quad.size.height==0.25f);
    CHECK(quad.pose.position.z==3.125f && quad.subImage.swapchain==chain && quad.subImage.imageArrayIndex==0);
    CHECK(quad.subImage.imageRect.offset.x==2 && quad.subImage.imageRect.offset.y==3 &&
          quad.subImage.imageRect.extent.width==40 && quad.subImage.imageRect.extent.height==50);
}
}
int main() {
    try {
        CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
            D3D11_SDK_VERSION,&r.device,nullptr,&r.context)));
        LifecycleAndPixels();Refusals();Calls({"destroy"});
        PresentationPolicy();
        HudShapeAndRefusalReasons();Calls({});
        SizeFloor();
        Formats();Acquired();StaticLayers();PaintedLayers();Poses();
        std::cout<<"layer_swapchain PASS: real WARP copy/alpha, independent world texture, lifetime, resize, inherited linear/sRGB format, XR failures, refusal reasons, per-layer size floor, format choice, acquired images, static and painted layers, poses\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
