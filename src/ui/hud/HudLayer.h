#pragma once
#include <windows.h>
#include <d3d11.h>
#include <string>
namespace preyvr::dll {
DWORD SetHudLayerEnabled(unsigned enabled);
bool HudLayerEnabled();
void RefuseHudLayer(const char* reason);
std::string HudLayerReport();
// Render-thread frame-boundary calls only. Drain once even when XR skips a frame.
// The borrowed texture remains owned by the capture.
ID3D11Texture2D* HudLayerTexture();
// The text panel: subtitle and tutorial-card movies, captured with the HUD
// layer on into one texture. Same frame-boundary rule as the HUD.
// RefuseTextLayer returns them to the scene until the HUD layer is re-enabled.
ID3D11Texture2D* TextLayerTexture();
void RefuseTextLayer(const char* reason);
// The note panel: the note/book movie, captured the same way into its own
// texture, so XrSessionHost can hold it where it opened while the text panel
// follows the head. Same frame-boundary rule; RefuseNoteLayer as above.
ID3D11Texture2D* NoteLayerTexture();
void RefuseNoteLayer(const char* reason);
// The render-target substitution and movie display hooks the per-eye
// redraw needs, installed together. Then a draw made between the two
// targeted binds runs with `from` rebound to `to` wherever it asks for it.
// False when the hooks are missing or a capture is already in flight.
bool EnsureOverlayHooks();
bool DrawRedirected(ID3D11DeviceContext* context, ID3D11RenderTargetView* from, ID3D11DepthStencilView* fromDepth,
                    ID3D11RenderTargetView* to, ID3D11DepthStencilView* toDepth, void (*draw)(void*), void* argument);
// The HUD panel as this frame shows it (XrOverlayLayers BuildHudLayer): its size
// and its distance from the head, metres.
void SetHudLayerPresentation(bool active, float width=0, float height=0, float distance=1);
bool HudLayerReticle(float tanX, float tanY, float& x, float& y);
// **HUD Width and HUD Height** (VR Options hud_width, hud_height): how far round
// the HUD's corner groups reach (ammo, objectives, pickups, prompts), in percent
// of Prey's own layout, hud::SpreadMinPercent..SpreadMaxPercent, default 70 and
// 55. Prey pins them 5% in from the canvas edge, and the HUD's canvas is square
// in VR, so on a large panel they sat at the edge of the view
// (hud::CornerCoordinates). Applied to the HUD panel's draw and the wheel's
// replay, never to the wrist's. Any thread; the next HUD draw takes them.
DWORD SetHudWidthPercent(unsigned percent);
unsigned HudWidthPercent();
DWORD SetHudHeightPercent(unsigned percent);
unsigned HudHeightPercent();
// Additive native status copy. Request expires if XR stops consuming; no native
// status is removed. Calls and borrowed texture consumption are render-thread only.
void RequestNativeWristCapture(bool active);
struct NativeWristImage {
    ID3D11Texture2D* texture = nullptr; // this frame's capture, borrowed; null when none
    float frame[4]{};                   // the wrist frame in it, pixels (wrist::Layout::rect)
};
NativeWristImage NativeWristLayerTexture();
void ResetNativeWristCapture(); // render-thread session teardown
// **HUD parts with a layer of their own** (ui/hud/NativeHudIsolation.h Part).
// While its consumer requests it, a part leaves the gameplay HUD's own draw and
// is replayed alone into a texture the HUD's size -- only while its gate reads
// open (and through its close), so a closed part costs one boolean read. Not
// requested, or on any refusal, the HUD draws it where Prey does. Render thread:
// request once per XR frame the consumer can show it; take once per frame.
enum class HudPart : unsigned { wheel, reticle, count };
void RequestHudPart(HudPart part, bool active);
struct HudPartImage {
    ID3D11Texture2D* texture = nullptr; // this frame's replay, borrowed; null when not lifted
    bool drawn = false;                 // the replay reached the part (not hidden by Prey)
    int open = -1;                      // its gate: 1 open, 0 closed, -1 unread or unreadable
};
HudPartImage TakeHudPart(HudPart part);
// The consumer cannot show it (its swapchain was refused): back to the HUD's
// own draw until the XR session restarts.
void RefuseHudPart(HudPart part, const char* reason);
void ResetHudParts(); // render-thread session teardown
// A number in a movie by path (e.g. "_root.ar_mc.ar.12._x"), read or written
// through the movie root's own GetVariable/SetVariable. Render thread, inside
// that movie's display callback only. False when the path does not resolve or
// is not a number.
bool HudMovieNumber(void* root,const char* path,double& value);
bool SetHudMovieNumber(void* root,const char* path,double value);
// hud.var / hud.setvar (diagnostics): queued, run in the next gameplay HUD
// display callback. The report lists each path's value after the operation.
DWORD QueueHudVariable(const std::string& path,bool set,double value);
std::string HudVariableReport();
}
