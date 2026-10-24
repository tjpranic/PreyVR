"""Byte check of the movement re-poll chain (docs/MOVE-STOP-2026-10-24.md).

Prey's player input re-reads the move and look axes from the physical gamepad's
input symbols on an action-map or filter event, overwriting what the mod's
synthetic posts stored. Every link below is checked against the Steam PreyDll.dll
(SHA-256 7D6E322F...05311A7), from the file, without Ghidra.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools" / "re"))
from preydll import Image  # noqa: E402

img = Image()
ok = True


def check(rva, hexs, what):
    global ok
    want = bytes.fromhex(hexs)
    got = img.at(rva, len(want))
    match = got == want
    ok &= match
    print(f"{rva:#09x} {'OK ' if match else 'BAD'} {got.hex():<30} {what}")


def target(what, got, want):
    global ok
    match = got == want
    ok &= match
    print(f"{'':9} {'OK ' if match else 'BAD'} {what}: {got:#x} (want {want:#x})")


print("== ArkPlayerInput (player+0x8E0, vtable 0x1E580E0): the action-map listener")
# Second base (input+8), vtable 0x1E580F8: slot 0 destructor thunk, slot 1 OnActionMapEvent.
target("listener vtable 0x1E580F8 +0x8", img.vtable_target(0x1E580F8 + 0x8), 0x1590390)
check(0x15903B2, "83f803", "cmp eax,3                  (event: action map status changed)")
check(0x15903BB, "48837a1000", "cmp qword [rdx+10h],0      (lparam: enabled, else return)")
check(0x15903CD, "ff90c8000000", "call [rax+0C8h]            (the map's GetName)")
check(0x15903D6, "488d15c7d77000", 'lea rdx,"player" (0x1C9DBA4)')
target("type-3 repoll call 0x15903F0", img.call_target(0x15903F0), 0x158E780)
check(0x15904BC, "83f802", "cmp eax,2                  (event: filter status changed)")
check(0x15904C1, "488b4110", "mov rax,[rcx+10h]          (mode stack end, input+0x18)")
check(0x15904C5, "48394108", "cmp [rcx+8],rax            (stack begin, input+0x10: non-empty)")
check(0x15904CB, "8378f800", "cmp dword [rax-8],0        (top mode == 0: player)")
target("type-2 repoll call 0x15904D6", img.call_target(0x15904D6), 0x158E780)

print("== FUN_18158e780: the repoll overwrites the axes")
check(0x158E976, "4881c2f0020000", "add rdx,2F0h               (action table: xi_movex)")
target("poll call 0x158E981", img.call_target(0x158E981), 0x158EB90)
check(0x158E99A, "f30f11435c", "movss [rbx+5Ch],xmm0       (move X := polled)")
check(0x158E9EB, "4881c2f8020000", "add rdx,2F8h               (xi_movey)")
target("poll call 0x158E9F2", img.call_target(0x158E9F2), 0x158EB90)
check(0x158EA03, "f30f114660", "movss [rsi+60h],xmm0       (move Y := polled)")
check(0x158EA51, "4881c210030000", "add rdx,310h               (xi_rotatepitch)")
check(0x158EA85, "f30f114644", "movss [rsi+44h],xmm0       (pitch := -polled)")
check(0x158EA9A, "4881c208030000", "add rdx,308h               (xi_rotateyaw)")
check(0x158EAC6, "f30f11464c", "movss [rsi+4Ch],xmm0       (yaw := -polled)")

print("== the action table the offsets index (initializer 0x1706DA0)")
check(0x1708B3B, "488d8bf0020000", "lea rcx,[rbx+2F0h]  <- " + img.cstring(img.rip_target(0x1708B34, 7, 3)))
check(0x1708B74, "488d8b08030000", "lea rcx,[rbx+308h]  <- " + img.cstring(img.rip_target(0x1708B6D, 7, 3)))
check(0x1708B87, "488d8b10030000", "lea rcx,[rbx+310h]  <- " + img.cstring(img.rip_target(0x1708B80, 7, 3)))

print("== FUN_18158eb90: what the poll reads")
check(0x158EBA9, "ff90a8010000", "call [rax+1A8h]            (manager: action filtered -> 0)")
check(0x158EBF7, "448b423c", "mov r8d,[rdx+3Ch]          (CUIManager+0x3C: pad index)")
check(0x158EBFB, "ba03000000", "mov edx,3                  (device type: gamepad)")
check(0x158EC03, "ff90d8000000", "call [rax+0D8h]            (IInput LookupSymbol)")
check(0x158EC0E, "f30f10401c", "movss xmm0,[rax+1Ch]       (SInputSymbol::value)")
target("CActionMapManager vtable +0x1A8", img.vtable_target(0x1CBFC48 + 0x1A8), 0x3CDFE0)

print("== the analog handlers the mod's posts reach")
check(0x158FD32, "f30f11415c", "movss [rcx+5Ch],xmm0       (xi_movex handler 0x158FD20)")
check(0x158FDB4, "f30f114960", "movss [rcx+60h],xmm1       (xi_movey handler 0x158FD80)")
check(0x158FE68, "0f570501ed6e00", "xorps xmm0,[signmask]      (yaw handler 0x158FE50 negates)")
check(0x158FE6F, "f30f11414c", "movss [rcx+4Ch],xmm0       (yaw handler stores)")

print("== the posts never set the symbol the poll reads (PostInputEvent 0x9D6D30)")
target("listener dispatch 0x9D6DEE", img.call_target(0x9D6DEE), 0x9D7430)
check(0x9D6E0D, "488b5328", "mov rdx,[rbx+28h]          (event symbol pointer)")
check(0x9D6E11, "4885d2", "test rdx,rdx               (null: no symbol bookkeeping)")

print("== what broadcasts the events")
target("manager vtable +0xD8 (EnableActionMap)", img.vtable_target(0x1CBFC48 + 0xD8), 0x3CF050)
check(0x3CF0D3, "ff90d8000000", "call [rax+0D8h]            (map: is enabled)")
check(0x3CF0D9, "403ac5", "cmp al,bpl                 (broadcast only on a change)")
check(0x3CF119, "c744242003000000", "mov dword [rsp+20h],3      (event type 3)")
target("type-3 broadcast 0x3CF121", img.call_target(0x3CF121), 0x3CE5C0)
target("manager vtable +0xE0 (EnableFilter)", img.vtable_target(0x1CBFC48 + 0xE0), 0x3CF1C0)
target("filter vtable 0x1CBE190 +0x38 (Enable)", img.vtable_target(0x1CBE190 + 0x38), 0x3C3770)
check(0x3C3853, "c744242002000000", "mov dword [rsp+20h],2      (event type 2, no change test)")
target("type-2 broadcast 0x3C3860", img.call_target(0x3C3860), 0x3CE5C0)

print("== the explicit movement reset (pause transition 0x119D360, door pry 0x1456680)")
check(0x158EDE0, "33c04889415c89416cc3", "xor eax,eax; mov [rcx+5Ch],rax; mov [rcx+6Ch],eax; ret")
target("reset call in pause transition", img.call_target(0x119D421), 0x158EDE0)
target("reset call in door pry", img.call_target(0x145672B), 0x158EDE0)

print("all OK" if ok else "MISMATCH")
sys.exit(0 if ok else 1)
