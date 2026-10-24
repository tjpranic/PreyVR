#include "LayerSwapchain.h"
#include <algorithm>
#include <sstream>

namespace preyvr::dll {
namespace {
int Family(DXGI_FORMAT format) {
    switch(format) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:return 1;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:return 2;
    default:return 0;
    }
}
bool Supported(const D3D11_TEXTURE2D_DESC& d,UINT minWidth,UINT minHeight) {
    return Family(d.Format)!=0 && d.Width>=minWidth && d.Height>=minHeight &&
        d.Width<=kMaxLayerExtent && d.Height<=kMaxLayerExtent && d.ArraySize==1 && d.MipLevels==1 &&
        d.SampleDesc.Count==1 && d.SampleDesc.Quality==0;
}
bool Compatible(const D3D11_TEXTURE2D_DESC& a,const D3D11_TEXTURE2D_DESC& b,UINT minWidth,UINT minHeight) {
    return Supported(a,minWidth,minHeight)&&Supported(b,minWidth,minHeight)&&
        a.Width==b.Width&&a.Height==b.Height&&Family(a.Format)==Family(b.Format);
}
}
bool InventoryTextureCompatible(const D3D11_TEXTURE2D_DESC& a,const D3D11_TEXTURE2D_DESC& b) {
    return Compatible(a,b,kLayerMinWidth,kLayerMinHeight);
}
const char* LayerSwapchain::RefusalName(Refusal refusal) {
    switch(refusal) {
    case Refusal::none:return "none";
    case Refusal::session:return "no_session";
    case Refusal::source:return "source_texture";
    case Refusal::presentationFormat:return "presentation_format";
    case Refusal::enumerateFormats:return "enumerate_formats";
    case Refusal::formatNotListed:return "format_not_listed";
    case Refusal::create:return "create_swapchain";
    case Refusal::images:return "enumerate_images";
    case Refusal::image:return "image_incompatible";
    }
    return "unknown";
}
void LayerSwapchain::Reset() {
    if(chain_)api_.destroy(chain_);
    chain_=XR_NULL_HANDLE;session_=XR_NULL_HANDLE;images_.clear();desc_={};
    presentationFormat_=DXGI_FORMAT_UNKNOWN;
    attempted_=fault_=false;
    refusal_=Refusal::none;result_=XR_SUCCESS;
}
bool LayerSwapchain::Prepare(XrSession session,const D3D11_TEXTURE2D_DESC& source,DXGI_FORMAT presentationFormat) {
    if(attempted_ && session_==session && presentationFormat_==presentationFormat && desc_.Format==source.Format &&
       Compatible(desc_,source,minWidth_,minHeight_))return chain_!=XR_NULL_HANDLE&&!fault_;
    Reset();attempted_=true;session_=session;desc_=source;
    presentationFormat_=presentationFormat;
    auto refuse=[&](Refusal why,XrResult result=XR_SUCCESS){refusal_=why;result_=result;return false;};
    if(!session)return refuse(Refusal::session);
    if(!Supported(source,minWidth_,minHeight_))return refuse(Refusal::source);
    if((presentationFormat!=DXGI_FORMAT_R8G8B8A8_UNORM && presentationFormat!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
        presentationFormat!=DXGI_FORMAT_B8G8R8A8_UNORM && presentationFormat!=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) ||
       Family(presentationFormat)!=Family(source.Format))return refuse(Refusal::presentationFormat);
    uint32_t count=0;
    XrResult result=api_.formats(session,0,&count,nullptr);
    if(result!=XR_SUCCESS || !count || count>256)return refuse(Refusal::enumerateFormats,result);
    std::vector<int64_t> formats(count);
    result=api_.formats(session,count,&count,formats.data());
    if(result!=XR_SUCCESS || count>formats.size())return refuse(Refusal::enumerateFormats,result);
    formats.resize(count);
    // Inherit the actual main/menu choice, not an independent sRGB preference.
    // CopyResource preserves bytes; it neither converts gamma nor swizzles.
    const int64_t format=presentationFormat;
    if(std::find(formats.begin(),formats.end(),format)==formats.end())return refuse(Refusal::formatNotListed);
    XrSwapchainCreateInfo create{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    create.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    create.format=format;create.sampleCount=1;create.width=source.Width;create.height=source.Height;
    create.faceCount=1;create.arraySize=1;create.mipCount=1;
    result=api_.create(session,&create,&chain_);
    if(result!=XR_SUCCESS || !chain_) {chain_=XR_NULL_HANDLE;return refuse(Refusal::create,result);}
    auto discard=[&](Refusal why,XrResult failed=XR_SUCCESS){
        api_.destroy(chain_);chain_=XR_NULL_HANDLE;images_.clear();return refuse(why,failed);};
    count=0;
    result=api_.images(chain_,0,&count,nullptr);
    if(result!=XR_SUCCESS || !count || count>64)return discard(Refusal::images,result);
    images_.resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    result=api_.images(chain_,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images_.data()));
    if(result!=XR_SUCCESS || !count || count>images_.size())return discard(Refusal::images,result);
    images_.resize(count);
    for(const auto& image:images_) {
        if(!image.texture)return discard(Refusal::image);
        D3D11_TEXTURE2D_DESC d{};image.texture->GetDesc(&d);
        if(!Compatible(source,d,minWidth_,minHeight_))return discard(Refusal::image);
    }
    return true;
}
LayerSwapchain::Upload LayerSwapchain::Copy(ID3D11DeviceContext* context,ID3D11Texture2D* source) {
    if(fault_)return Upload::sessionFault;
    if(!chain_ || !context || !source)return Upload::failed;
    D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);
    if(!Compatible(desc_,d,minWidth_,minHeight_))return Upload::failed;
    uint32_t index=0;
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if(api_.acquire(chain_,&acquire,&index)!=XR_SUCCESS)return Upload::failed;
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout=50000000; // 50 ms; no unbounded render-thread wait.
    if(api_.wait(chain_,&wait)!=XR_SUCCESS) {
        // XR_TIMEOUT_EXPIRED is positive, but it does NOT permit copy/release.
        fault_=true;return Upload::sessionFault;
    }
    const bool valid=index<images_.size();
    if(valid)context->CopyResource(images_[index].texture,source);
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if(api_.release(chain_,&release)!=XR_SUCCESS) {fault_=true;return Upload::sessionFault;}
    return valid?Upload::ready:Upload::failed;
}
DXGI_FORMAT FirstListedFormat(PFN_xrEnumerateSwapchainFormats formats,XrSession session,
                              std::initializer_list<DXGI_FORMAT> preferred) {
    uint32_t count=0;
    if(formats(session,0,&count,nullptr)!=XR_SUCCESS || !count || count>=256)return DXGI_FORMAT_UNKNOWN;
    std::vector<int64_t> listed(count);
    if(formats(session,count,&count,listed.data())!=XR_SUCCESS)return DXGI_FORMAT_UNKNOWN;
    for(const auto format:preferred)
        if(std::find(listed.begin(),listed.end(),format)!=listed.end())return format;
    return DXGI_FORMAT_UNKNOWN;
}
void AcquiredImage::Acquire(const LayerSwapchainApi& api,XrSwapchain swapchain) {
    release_=api.release;swapchain_=swapchain;
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    acquired_=XR_SUCCEEDED(api.acquire(swapchain,&acquire,&index_));
    if(!acquired_)return;
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout=XR_INFINITE_DURATION;
    waited_=XR_SUCCEEDED(api.wait(swapchain,&wait));
}
bool AcquiredImage::Release() {
    if(!waited_)return false;
    waited_=false;
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    return XR_SUCCEEDED(release_(swapchain_,&release));
}
StaticUpload CreateStaticLayer(const LayerSwapchainApi& api,XrSession session,ID3D11Device* device,int64_t format,
                               uint32_t width,uint32_t height,const void* pixels,XrSwapchain& swapchain) {
    StaticUpload out;
    XrSwapchainCreateInfo create{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    create.createFlags=XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT;
    create.usageFlags=XR_SWAPCHAIN_USAGE_SAMPLED_BIT|XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    create.format=format;create.sampleCount=1;create.width=width;create.height=height;
    create.faceCount=1;create.arraySize=1;create.mipCount=1;
    if(XR_FAILED(api.create(session,&create,&swapchain)))return out;
    uint32_t count=0;
    if(XR_FAILED(api.images(swapchain,0,&count,nullptr)))return out;
    std::vector<XrSwapchainImageD3D11KHR> images(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    if(XR_FAILED(api.images(swapchain,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()))))
        return out;
    AcquiredImage image(api,swapchain);
    if(!image.Acquired())return out;
    if(!image.Waited()){out.waitFailed=true;return out;}
    out.waited=true;
    if(image.Index()<images.size()) {
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;device->GetImmediateContext(&context);
        if(context){context->UpdateSubresource(images[image.Index()].texture,0,nullptr,pixels,width*4,0);out.ready=true;}
    }
    if(!image.Release())out.ready=false;
    return out;
}
bool PaintedLayer::Ensure(const LayerSwapchainApi& api,XrSession session,ID3D11Device* device,
                          void (*log)(const std::string&)) {
    if(attempted_)return Ready();
    attempted_=true;
    format_=FirstListedFormat(api.formats,session,{DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB});
    if(format_==DXGI_FORMAT_UNKNOWN) {
        log(std::string("result=refused detail=")+detail_+" reason=no_srgb_format");
        return false;
    }
    D3D11_TEXTURE2D_DESC d{};d.Width=width_;d.Height=height_;d.Format=format_;
    d.SampleDesc.Count=d.ArraySize=d.MipLevels=1;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    if(FAILED(device->CreateTexture2D(&d,nullptr,&texture_)))return false;
    swapchain_=std::make_unique<LayerSwapchain>(api,width_,height_);
    if(!swapchain_->Prepare(session,d,format_)) {
        std::ostringstream line;
        line<<"result=refused detail="<<detail_<<" reason="<<LayerSwapchain::RefusalName(swapchain_->LastRefusal())
            <<" xrResult="<<swapchain_->LastResult()<<" format="<<format_;
        log(line.str());
        swapchain_.reset();
        return false;
    }
    log(std::string("result=0 detail=")+detail_+" format="+std::to_string(format_));
    return true;
}
LayerSwapchain::Upload PaintedLayer::Paint(ID3D11DeviceContext* context,const void* pixels) {
    context->UpdateSubresource(texture_.Get(),0,nullptr,pixels,width_*4,0);
    return swapchain_->Copy(context,texture_.Get());
}
void PaintedLayer::Reset() {
    swapchain_.reset();texture_.Reset();
    attempted_=false;format_=DXGI_FORMAT_UNKNOWN;
}
}
