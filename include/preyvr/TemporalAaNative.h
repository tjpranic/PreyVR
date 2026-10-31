#pragma once
#include <cstddef>
#include <cstdint>
// Prey's temporal-AA state, which the probe reads. Steam SHA 7d6e322f...11a7.
// See docs/TEMPORAL-AA-SCOPE-2026-10-30.md and R-136. Static contracts, checked
// by tools/re/verify_temporal_aa.py.
namespace preyvr::taa::native {
// The post-processing counter: +1, kept in 24 bits, at the end of every
// post-processing run (the driver 0xF85230, at 0xF8567E); otherwise only the
// static initialiser writes it. It picks the history slot, the AA stage's ring
// slot and the jitter sample.
inline constexpr std::uintptr_t FrameCounter=0x2BB5150;
// r_AntialiasingMode's storage, and the copy the stages read (0xFE3760),
// clamped to 5 by 0xECEC10.
inline constexpr std::uintptr_t ModeCvar=0x2B1C74C,ModeEffective=0x2B1C750;
// $PrevBackBuffer0 and 1, one CTexture* each: the resolve writes [c & 1] and
// reads [(c + 1) & 1]. A CTexture's device texture is at +0x30 (R-132).
inline constexpr std::uintptr_t History=0x2B3F690;
inline constexpr std::size_t HistorySlots=2,TextureDevice=0x30;
// The graphics pipeline (0xFCE940: mov rcx,[Pipeline]; jmp 0xFB2A00). Its AA
// stage is at +0x288 (0xFB2A0E), and the render view it draws at +0x2E0 (0xFB1670).
inline constexpr std::uintptr_t Pipeline=0x2B2FC50;
inline constexpr std::size_t PipelineAaStage=0x288,PipelineView=0x2E0;
// The AA stage: the counter its last resolve ran with (+0x20, 0xF9BAFA).
inline constexpr std::size_t StageLastResolve=0x20;
// CD3D9Renderer: the view-projection this run built (+0xCF0) and the previous
// run's (+0xD30), which the driver copies from it at the end of post-processing
// (0xF8559F); the sub-pixel jitter (+0xD70, R-129).
inline constexpr std::size_t RendererViewProj=0xCF0,RendererPrevViewProj=0xD30,ViewProjSize=0x40;
inline constexpr std::size_t RendererJitter=0xD70;
// The renderer's previous view per viewport: 8 slots of 0x40 from +0xAB0, the
// viewport's chosen by +0x8060 (0..7, RT_SetViewport 0xF7EA90). Only the post
// driver writes one (0xFE7440, with this run's view from 0xFE49E0, at 0xF85593).
// Each moving object's motion matrix is its previous world matrix times this
// slot (0xFE5560 into 0xFD4570, from the instance setup 0xF001D0), the slot's
// translation dropped for first-person (FOB_NEAREST) objects.
inline constexpr std::size_t RendererPrevViews=0xAB0,RendererViewport=0x8060,PrevViewSlots=8,PrevViewSize=0x40;
// Moving objects' constants (above) are built when they compile, and the view's
// compile runs once: SwitchUsageMode (0xEE82F0) returns at once for an unchanged
// mode, and only its switch to Reading runs 0xEE46D0, which compiles (0xEFFCD0)
// into 0xF001D0. So the second eye's objects hold the first eye's previous view
// (preyvr/WorldMotion.h). This is the latched r_GraphicsPipeline: from 2 the
// passes draw those compiled objects (0xF8FD01), which only binds their constants
// (0xF00A90); below it each pass compiles per draw, each eye against its own view.
inline constexpr std::size_t RendererGraphicsPipeline=0x805C;
inline constexpr int CompiledOncePipeline=2;
// CRenderView::SetPreviousFrameCamera(view, const CCamera&) (R-064, view vtable
// slot 9): add rcx,0x13E0, then CCamera::operator= (0x111C70). The view's
// previous camera, which 0xFB1670 hands every pass that needs motion, is that
// CCamera at +0x13E0 (0x240 bytes).
inline constexpr std::uintptr_t SetPreviousFrameCamera=0xEE82D0;
inline constexpr std::size_t ViewPreviousCamera=0x13E0,CameraSize=0x240;
struct Contract {std::uintptr_t rva;std::size_t size;std::uint64_t hash;};
inline constexpr Contract Contracts[]={
    {0xF85230,1176,0x6E8597A5B7EEF719ull}, // PostDriver
    {0xF99FE0,7105,0xB3AF7ECC53E68C8Aull}, // TemporalResolve
    {0xFE3760,24,0x1FD113DAF27F1C35ull}, // AaModeFlags
    {0xECEC10,31,0xF3589FCCBB61F099ull}, // AaModeClamp
    {0xFB2A00,39,0x87C233554BCEC41Full}, // PipelineAaExecute
    {0xFB1670,615,0x382F015AC71C59F5ull}, // ViewInfo
    {0xEE82D0,12,0x409958AE6F013FD7ull}, // SetPreviousFrameCamera
    {0xFE7440,187,0xC1676179A7124364ull}, // PrevViewStore
    {0xFE5560,21,0x17A206F5BF784CB6ull}, // PrevViewSlot
    {0xF7EA90,138,0x73EFC2342254ECF9ull}, // SetViewport
    {0xEE46D0,681,0x43D502367BF57B72ull}, // CompileObjects
    {0xF001D0,1652,0xDCDF05B0D549FA3Full}, // InstanceConstants
    {0xFD4150,373,0x61689218A57433D4ull}, // PrevWorld
    {0xFD4570,2371,0x91387703389400CDull}, // PrevInViewSpace
    {0xF00A90,500,0xC39940F2DB651CD3ull}, // DrawCompiled
};
}
