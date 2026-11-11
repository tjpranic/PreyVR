#include "ui/hud/NativeHudIsolation.h"
#include "ui/hud/NativeWristTexture.h"
#include "ui/hud/NativeHudValue.h"
#include <d3d11sdklayers.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace preyvr;
using Microsoft::WRL::ComPtr;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
namespace {
struct Api {
    using Value=int;
    static constexpr int count=static_cast<int>(hud::ExcludedFromStatus.size()+hud::StatusMeters.size());
    std::array<int,count> objects{};
    int resolved=0,released=0,live=0,failResolve=-1,failRelease=-1;
    bool duplicate=false;
    bool Resolve(const char*,Value& value,void*& object){
        value=++resolved;++live;
        object=&objects[duplicate?0:resolved-1];
        return resolved-1!=failResolve;
    }
    bool Release(Value& value){
        if(!value)return true;
        value=0;--live;return released++!=failRelease;
    }
};
void Transactions(){
    for(int missing=0;missing<Api::count;++missing){
        Api api;api.failResolve=missing;
        {hud::Isolation guard(api);CHECK(!guard.Begin());CHECK(!guard.Excludes(&api.objects[0]));}
        CHECK(api.live==0&&api.released==missing+1);
    }
    Api api;
    {hud::Isolation guard(api);CHECK(guard.Begin());CHECK(!guard.Begin());CHECK(!guard.Complete());
        for(std::size_t i=0;i<hud::ExcludedFromStatus.size();++i)CHECK(guard.Excludes(&api.objects[i]));
        for(std::size_t i=hud::ExcludedFromStatus.size();i<api.objects.size();++i){
            CHECK(!guard.Excludes(&api.objects[i]));guard.Observe(&api.objects[i]);
            CHECK(guard.Complete()); // Other meters may be hidden (e.g. locked psi).
        }
        int unrelated=0;
        CHECK(guard.Complete());CHECK(!guard.Excludes(&unrelated));CHECK(guard.Release());CHECK(!guard.Complete());
        CHECK(!guard.Excludes(&api.objects[0]));
    }
    CHECK(api.live==0&&api.released==Api::count);
    api={};api.duplicate=true;
    {hud::Isolation guard(api);CHECK(!guard.Begin());}CHECK(api.live==0&&api.released==2);
    for(int failure=0;failure<Api::count;++failure){
        api={};api.failRelease=failure;
        {hud::Isolation guard(api);CHECK(guard.Begin());CHECK(!guard.Release());CHECK(api.live==0);}
        CHECK(api.live==0&&api.released==Api::count);
    }
}
struct Rect {unsigned x0,y0,x1,y1;};
struct Graphics {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;ComPtr<ID3D11InfoQueue> debug;
    Graphics(){
        auto hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_DEBUG,
            nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
        if(FAILED(hr))hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
            D3D11_SDK_VERSION,&device,nullptr,&context);
        CHECK(SUCCEEDED(hr));device.As(&debug);
    }
    // Coloured premultiplied rectangles: unless `blank`, one touching the
    // bottom-left input edge, and any `extra`.
    ComPtr<ID3D11Texture2D> Source(unsigned w,unsigned h,DXGI_FORMAT format,bool blank=false,std::vector<Rect> extra={}){
        std::vector<std::uint32_t> pixels(w*h);
        const bool bgra=format==DXGI_FORMAT_B8G8R8A8_UNORM||format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        std::vector<Rect> rects;if(!blank)rects.push_back({0,h-50,100,h});
        for(auto r:extra)rects.push_back(r);
        for(auto r:rects)for(unsigned y=r.y0;y<r.y1;++y)for(unsigned x=r.x0;x<r.x1;++x)
            pixels[y*w+x]=bgra?0x80643219u:0x80193264u;
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.Format=format;d.ArraySize=d.MipLevels=1;
        d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{pixels.data(),w*4,0};ComPtr<ID3D11Texture2D> texture;
        CHECK(SUCCEEDED(device->CreateTexture2D(&d,&data,&texture)));return texture;
    }
    std::vector<std::uint32_t> Read(ID3D11Texture2D* texture){
        CHECK(texture);D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
        CHECK(d.Width==dll::NativeWristWidth&&d.Height==dll::NativeWristHeight&&d.Format==DXGI_FORMAT_R8G8B8A8_UNORM);
        d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;CHECK(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&staging)));
        context->CopyResource(staging.Get(),texture);D3D11_MAPPED_SUBRESOURCE m{};
        CHECK(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m)));
        std::vector<std::uint32_t> out(d.Width*d.Height);
        for(unsigned y=0;y<d.Height;++y)
            std::memcpy(&out[y*d.Width],static_cast<const char*>(m.pData)+y*m.RowPitch,d.Width*4);
        context->Unmap(staging.Get(),0);return out;
    }
    // The frame used below: the source's bottom-left 275 x 120, the card's aspect,
    // eight card pixels per source pixel. The block (100 x 50) covers card
    // x 0..800, y 560..960.
    static void Frame(unsigned h,float (&frame)[4]){frame[0]=0;frame[1]=float(h-120);frame[2]=275;frame[3]=float(h);}
    void Inspect(ID3D11Texture2D* texture,bool blank){
        const auto pixels=Read(texture);
        const unsigned w=dll::NativeWristWidth,h=dll::NativeWristHeight;
        if(blank){for(auto v:pixels)CHECK(v==0);return;}
        CHECK(pixels[760*w+400]==0x80193264u); // Exact swizzle, alpha and gamma-byte preservation.
        CHECK(pixels[760*w+1200]==0&&pixels[200*w+400]==0); // beside and above the block: nothing
        unsigned x0=w,y0=h,x1=0,y1=0;
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)
            if((pixels[y*w+x]>>24)>16){x0=std::min(x0,x);y0=std::min(y0,y);x1=std::max(x1,x);y1=std::max(y1,y);}
        CHECK(x0==0&&y1==h-1);                     // the frame's corner is the block's
        CHECK(x1>=796&&x1<=806&&y0>=552&&y0<=562); // mapped, not fitted: 8x, wherever it is
    }
    void NoErrors(){
        if(!debug)return;
        for(UINT64 i=0;i<debug->GetNumStoredMessages();++i){
            SIZE_T n=0;debug->GetMessage(i,nullptr,&n);std::vector<char> storage(n);
            auto message=reinterpret_cast<D3D11_MESSAGE*>(storage.data());debug->GetMessage(i,message,&n);
            if(message->Severity<=D3D11_MESSAGE_SEVERITY_WARNING)throw std::runtime_error(message->pDescription);
        }
    }
};
// **No resizing**: whatever else the capture holds, inside the frame or out of
// it, the block keeps its place and size on the card, pixel for pixel.
void FixedFrame(){
    Graphics g;dll::NativeWristTexture fitter;float frame[4];Graphics::Frame(480,frame);
    auto alone=g.Source(640,480,DXGI_FORMAT_R8G8B8A8_UNORM);
    const auto before=g.Read(fitter.Fit(g.context.Get(),alone.Get(),frame));
    // An effect above, outside the frame, and a power beside it, inside: the old
    // fit shrank the block to make room for them.
    auto busy=g.Source(640,480,DXGI_FORMAT_R8G8B8A8_UNORM,false,{{300,0,400,50},{180,400,260,440}});
    const auto after=g.Read(fitter.Fit(g.context.Get(),busy.Get(),frame));
    const unsigned w=dll::NativeWristWidth,h=dll::NativeWristHeight;
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<800;++x)CHECK(before[y*w+x]==after[y*w+x]);
    CHECK(after[480*w+1760]==0x80193264u); // the one inside shows, where it is
    // A frame that is not one: nothing.
    const float nan[4]{0,0,NAN,1},empty[4]{10,10,10,20};
    CHECK(!fitter.Fit(g.context.Get(),alone.Get(),nan)&&!fitter.Fit(g.context.Get(),alone.Get(),empty));
    g.NoErrors();
}
// The fade scales all four (premultiplied) channels; a refit is the same pixels
// with a new fade. No glass: around the meters the card is clear.
void Fade(){
    Graphics g;dll::NativeWristTexture fitter;float frame[4];Graphics::Frame(480,frame);
    auto input=g.Source(640,480,DXGI_FORMAT_R8G8B8A8_UNORM);
    const unsigned w=dll::NativeWristWidth;
    const auto half=g.Read(fitter.Fit(g.context.Get(),input.Get(),frame,.5f))[760*w+400];
    for(int k=0;k<4;++k){
        const int full=(0x80193264u>>(8*k))&0xFF,got=(half>>(8*k))&0xFF;
        CHECK(std::abs(got-full/2)<=1);
    }
    const auto card=g.Read(fitter.Refit(g.context.Get(),1.f));
    CHECK(card[760*w+400]==0x80193264u&&card[0]==0&&card[760*w+1600]==0&&card[200*w+400]==0);
    const auto quarter=g.Read(fitter.Refit(g.context.Get(),.25f))[760*w+400]>>24;
    CHECK(std::abs(static_cast<int>(quarter)-0x80/4)<=1);
    g.NoErrors();
}
// wrist.report's content=: the box the capture really held, read back on a later
// frame without waiting, with the rectangle that capture was mapped with.
void Content(){
    Graphics g;dll::NativeWristTexture fitter;float frame[4];Graphics::Frame(480,frame);
    CHECK(!fitter.LastContent().valid);
    auto input=g.Source(640,480,DXGI_FORMAT_R8G8B8A8_UNORM,false,{{300,0,400,50}});
    CHECK(fitter.Fit(g.context.Get(),input.Get(),frame));
    for(int i=0;i<500&&!fitter.LastContent().valid;++i){g.context->Flush();fitter.Refit(g.context.Get(),1.f);}
    const auto c=fitter.LastContent();
    CHECK(c.valid&&!c.empty&&c.serial==1);
    CHECK(c.box[0]==0&&c.box[1]==0&&c.box[2]==400&&c.box[3]==480); // what lies outside the frame counts
    CHECK(c.frame[0]==frame[0]&&c.frame[1]==frame[1]&&c.frame[2]==frame[2]&&c.frame[3]==frame[3]);
    // One capture in kContentEvery is sampled: the ones between are not.
    auto blank=g.Source(640,480,DXGI_FORMAT_R8G8B8A8_UNORM,true);
    for(unsigned i=1;i<dll::NativeWristTexture::kContentEvery;++i)CHECK(fitter.Fit(g.context.Get(),blank.Get(),frame));
    for(int i=0;i<50;++i){g.context->Flush();fitter.Refit(g.context.Get(),1.f);}
    CHECK(fitter.LastContent().serial==1);
    CHECK(fitter.Fit(g.context.Get(),blank.Get(),frame));
    for(int i=0;i<500&&fitter.LastContent().serial==1;++i){g.context->Flush();fitter.Refit(g.context.Get(),1.f);}
    CHECK(fitter.LastContent().serial==2&&fitter.LastContent().empty);
    g.NoErrors();
}
void Pixels(){
    Graphics g;dll::NativeWristTexture fitter;float frame[4];Graphics::Frame(480,frame);
    auto sentinel=g.Source(640,480,DXGI_FORMAT_R8G8B8A8_UNORM);
    ComPtr<ID3D11ShaderResourceView> view;CHECK(SUCCEEDED(g.device->CreateShaderResourceView(sentinel.Get(),nullptr,&view)));
    auto v=view.Get();g.context->CSSetShaderResources(0,1,&v);g.context->CSSetShaderResources(1,1,&v);
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
    ComPtr<ID3D11Buffer> buffer;CHECK(SUCCEEDED(g.device->CreateBuffer(&bd,nullptr,&buffer)));
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};ud.Format=DXGI_FORMAT_R32_UINT;ud.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;ud.Buffer.NumElements=4;
    ComPtr<ID3D11UnorderedAccessView> uav;CHECK(SUCCEEDED(g.device->CreateUnorderedAccessView(buffer.Get(),&ud,&uav)));
    auto u=uav.Get();g.context->CSSetUnorderedAccessViews(0,1,&u,nullptr);
    constexpr char code[]="[numthreads(1,1,1)] void main(){}";
    ComPtr<ID3DBlob> bytes;CHECK(SUCCEEDED(D3DCompile(code,sizeof(code)-1,nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&bytes,nullptr)));
    ComPtr<ID3D11ComputeShader> shader;CHECK(SUCCEEDED(g.device->CreateComputeShader(bytes->GetBufferPointer(),bytes->GetBufferSize(),nullptr,&shader)));
    g.context->CSSetShader(shader.Get(),nullptr,0);
    D3D11_QUERY_DESC qd{D3D11_QUERY_OCCLUSION_PREDICATE,0};ComPtr<ID3D11Predicate> predicate;
    CHECK(SUCCEEDED(g.device->CreatePredicate(&qd,&predicate)));
    g.context->Begin(predicate.Get());g.context->End(predicate.Get());g.context->SetPredication(predicate.Get(),TRUE);
    for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                     DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB}){
        auto input=g.Source(640,480,format);g.Inspect(fitter.Fit(g.context.Get(),input.Get(),frame),false);
        ComPtr<ID3D11ShaderResourceView> before0,before1;
        g.context->CSGetShaderResources(0,1,&before0);g.context->CSGetShaderResources(1,1,&before1);
        CHECK(before0.Get()==view.Get()&&before1.Get()==view.Get());
        ComPtr<ID3D11ComputeShader> previousShader;ComPtr<ID3D11UnorderedAccessView> previousUav;
        ComPtr<ID3D11Predicate> previousPredicate;BOOL condition=FALSE;
        g.context->CSGetShader(&previousShader,nullptr,nullptr);g.context->CSGetUnorderedAccessViews(0,1,&previousUav);
        g.context->GetPredication(&previousPredicate,&condition);
        CHECK(previousShader.Get()==shader.Get()&&previousUav.Get()==uav.Get());
        CHECK(previousPredicate.Get()==predicate.Get()&&condition==TRUE);
    }
    g.context->SetPredication(nullptr,FALSE);
    // A larger capture with its frame in the same corner: the same card.
    float tall[4];Graphics::Frame(720,tall);
    auto bigger=g.Source(1280,720,DXGI_FORMAT_R8G8B8A8_UNORM);g.Inspect(fitter.Fit(g.context.Get(),bigger.Get(),tall),false);
    auto clear=g.Source(1280,720,DXGI_FORMAT_R8G8B8A8_UNORM,true);g.Inspect(fitter.Fit(g.context.Get(),clear.Get(),tall),true);
    CHECK(!fitter.Fit(nullptr,clear.Get(),tall)&&!fitter.Fit(g.context.Get(),nullptr,tall));
    Graphics other;auto otherInput=other.Source(640,480,DXGI_FORMAT_R8G8B8A8_UNORM);
    CHECK(!fitter.Fit(g.context.Get(),otherInput.Get(),frame));
    other.Inspect(fitter.Fit(other.context.Get(),otherInput.Get(),frame),false);
    g.Inspect(fitter.Fit(g.context.Get(),bigger.Get(),tall),false); // Recreate on device change.
    other.NoErrors();
    g.NoErrors();
}
// **Minifying**: a capture too small for the layout's density gives each card
// pixel more than one source pixel, four taps each, still where the frame says.
void Minify(){
    Graphics g;dll::NativeWristTexture fitter;
    auto input=g.Source(4096,2048,DXGI_FORMAT_R8G8B8A8_UNORM,true,{{0,1280,1600,2048}});
    const float frame[4]{0,512,3520,2048}; // 1.6 source pixels per card pixel
    const auto pixels=g.Read(fitter.Fit(g.context.Get(),input.Get(),frame));
    const unsigned w=dll::NativeWristWidth;
    CHECK(pixels[760*w+400]==0x80193264u&&pixels[760*w+1200]==0&&pixels[200*w+400]==0);
    g.NoErrors();
}
}
int main(){try{Transactions();Pixels();FixedFrame();Fade();Content();Minify();
    std::cout<<"native wrist isolation and GPU pixels PASS\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
