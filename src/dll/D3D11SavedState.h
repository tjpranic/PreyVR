#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <array>

namespace preyvr::dll {

// Every piece of pipeline state a mod draw on the game's immediate context can
// touch, restored on scope exit: input assembler, all five shader stages plus
// compute, slot 0 of the constant buffers, PS resource and sampler, rasterizer,
// viewports and scissors, blend, depth-stencil, render targets and predication.
// The debug overlay's draw and the ImGui options panel share it. ImGui's DX11
// backend restores most of this itself, but not the render targets, and it
// clears the hull, domain and compute shaders without restoring them.
struct D3D11SavedState {
    ID3D11DeviceContext* c;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> layout;
    D3D11_PRIMITIVE_TOPOLOGY topology{};
    Microsoft::WRL::ComPtr<ID3D11Buffer> vb; UINT stride = 0, offset = 0;
    Microsoft::WRL::ComPtr<ID3D11Buffer> ib; DXGI_FORMAT ibFormat{}; UINT ibOffset = 0;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs; std::array<ID3D11ClassInstance*, 256> vsInst{}; UINT vsCount = 256;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps; std::array<ID3D11ClassInstance*, 256> psInst{}; UINT psCount = 256;
    Microsoft::WRL::ComPtr<ID3D11GeometryShader> gs; std::array<ID3D11ClassInstance*, 256> gsInst{}; UINT gsCount = 256;
    Microsoft::WRL::ComPtr<ID3D11HullShader> hs; std::array<ID3D11ClassInstance*, 256> hsInst{}; UINT hsCount = 256;
    Microsoft::WRL::ComPtr<ID3D11DomainShader> ds; std::array<ID3D11ClassInstance*, 256> dsInst{}; UINT dsCount = 256;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> cs; std::array<ID3D11ClassInstance*, 256> csInst{}; UINT csCount = 256;
    Microsoft::WRL::ComPtr<ID3D11Buffer> vsCb, psCb;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> psSrv;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> psSampler;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rs;
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    UINT scissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend; float blendFactor[4]{}; UINT sampleMask = 0;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth; UINT stencilRef = 0;
    std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> rtvs{};
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;
    Microsoft::WRL::ComPtr<ID3D11Predicate> predicate; BOOL predicateValue = FALSE;

    explicit D3D11SavedState(ID3D11DeviceContext* context) : c(context)
    {
        c->IAGetInputLayout(&layout);
        c->IAGetPrimitiveTopology(&topology);
        c->IAGetVertexBuffers(0, 1, &vb, &stride, &offset);
        c->IAGetIndexBuffer(&ib, &ibFormat, &ibOffset);
        c->VSGetShader(&vs, vsInst.data(), &vsCount);
        c->PSGetShader(&ps, psInst.data(), &psCount);
        c->GSGetShader(&gs, gsInst.data(), &gsCount);
        c->HSGetShader(&hs, hsInst.data(), &hsCount);
        c->DSGetShader(&ds, dsInst.data(), &dsCount);
        c->CSGetShader(&cs, csInst.data(), &csCount);
        c->VSGetConstantBuffers(0, 1, &vsCb);
        c->PSGetConstantBuffers(0, 1, &psCb);
        c->PSGetShaderResources(0, 1, &psSrv);
        c->PSGetSamplers(0, 1, &psSampler);
        c->RSGetState(&rs);
        c->RSGetViewports(&viewportCount, viewports);
        c->RSGetScissorRects(&scissorCount, scissors);
        c->OMGetBlendState(&blend, blendFactor, &sampleMask);
        c->OMGetDepthStencilState(&depth, &stencilRef);
        c->OMGetRenderTargets(static_cast<UINT>(rtvs.size()), rtvs.data(), &dsv);
        c->GetPredication(&predicate, &predicateValue);
    }
    ~D3D11SavedState()
    {
        c->SetPredication(predicate.Get(), predicateValue);
        c->OMSetRenderTargets(static_cast<UINT>(rtvs.size()), rtvs.data(), dsv.Get());
        c->OMSetDepthStencilState(depth.Get(), stencilRef);
        c->OMSetBlendState(blend.Get(), blendFactor, sampleMask);
        c->RSSetScissorRects(scissorCount, scissors);
        c->RSSetViewports(viewportCount, viewports);
        c->RSSetState(rs.Get());
        ID3D11SamplerState* sampler = psSampler.Get();
        c->PSSetSamplers(0, 1, &sampler);
        ID3D11ShaderResourceView* srv = psSrv.Get();
        c->PSSetShaderResources(0, 1, &srv);
        ID3D11Buffer* cb = psCb.Get();
        c->PSSetConstantBuffers(0, 1, &cb);
        cb = vsCb.Get();
        c->VSSetConstantBuffers(0, 1, &cb);
        c->CSSetShader(cs.Get(), csInst.data(), csCount);
        c->DSSetShader(ds.Get(), dsInst.data(), dsCount);
        c->HSSetShader(hs.Get(), hsInst.data(), hsCount);
        c->GSSetShader(gs.Get(), gsInst.data(), gsCount);
        c->PSSetShader(ps.Get(), psInst.data(), psCount);
        c->VSSetShader(vs.Get(), vsInst.data(), vsCount);
        c->IASetIndexBuffer(ib.Get(), ibFormat, ibOffset);
        ID3D11Buffer* buffer = vb.Get();
        c->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
        c->IASetPrimitiveTopology(topology);
        c->IASetInputLayout(layout.Get());
        for (auto* p : rtvs) { if (p) { p->Release(); } }
        auto release = [](auto& list, UINT count) { for (UINT i = 0; i < count && i < list.size(); ++i) { if (list[i]) { list[i]->Release(); } } };
        release(vsInst, vsCount); release(psInst, psCount); release(gsInst, gsCount);
        release(hsInst, hsCount); release(dsInst, dsCount); release(csInst, csCount);
    }
};

} // namespace preyvr::dll
