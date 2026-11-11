#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include "ui/hud/WristFrame.h"
namespace preyvr::dll {
// The card's texture: the wrist frame's aspect (ui/hud/WristFrame.h).
inline constexpr unsigned NativeWristWidth=wrist::kTextureWidth,NativeWristHeight=wrist::kTextureHeight;
// Render-thread only. Maps the wrist frame's rectangle of the capture onto the
// card, with the colour bytes and premultiplication kept; `alpha` fades all four
// channels. **Nothing is fitted:** the rectangle comes from the replay's layout
// (wrist::Place), so the meters keep one size whatever Prey shows beside them.
// No readback in the path, no glass, no changes to native transforms.
class NativeWristTexture {
public:
    // `frame`: the rectangle in `source`, pixels (x0, y0, x1, y1).
    ID3D11Texture2D* Fit(ID3D11DeviceContext*,ID3D11Texture2D* source,const float (&frame)[4],float alpha=1.f);
    // The last source and rectangle again (no new capture this frame): a new fade, same pixels.
    ID3D11Texture2D* Refit(ID3D11DeviceContext*,float alpha);
    // **Diagnostics** (wrist.report content=): what the source held, the box
    // around every pixel with alpha above 0.003, from one capture in
    // kContentEvery, read back without waiting. `serial` counts the samples.
    struct Content {
        bool valid=false,empty=true;
        float box[4]{};    // source pixels
        float frame[4]{};  // the rectangle that capture was mapped with
        unsigned long long serial=0;
    };
    Content LastContent() const {return content_;}
    static constexpr unsigned kContentEvery=30;
private:
    ID3D11Texture2D* Run(ID3D11DeviceContext*,float alpha);
    void SampleContent(ID3D11DeviceContext*);
    void CollectContent(ID3D11DeviceContext*);
    bool Prepare(ID3D11Device*,const D3D11_TEXTURE2D_DESC&);
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> boundsShader_,fitShader_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> source_,output_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> sourceView_;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> boundsWrite_,outputWrite_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> bounds_,staging_,constants_;
    D3D11_TEXTURE2D_DESC description_{};
    float frame_[4]{},pendingFrame_[4]{};
    Content content_{};
    unsigned fits_=0;
    bool attempted_=false,retained_=false,pending_=false;
};
}
