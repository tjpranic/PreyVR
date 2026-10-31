"""Read-only Steam world-UI cursor contracts: why a terminal's or keypad's pointer lands off the controller's line
in the headset (a screen's cursor is cast from the player's cached reticle ray, whose origin the aim lane leaves at the
eye), and what the hand-origin fix and its watcher rely on (the cursor raycast reading the ray through IArkPlayer slot
0, from one call site; the ray built from the examination cursor while ArkPlayer +0xA48 is 1). Never attaches."""
import json,re,sys
from pathlib import Path
from preydll import DEFAULT_MODULE,Image,fnv1a64 as fnv,native_rows
ROOT=Path(__file__).resolve().parents[2]
BASE=0x180000000
CURSOR_RAY,READ_RETURN,GETTER,HIT=0x3017c0,0x301911,0x157cbb0,0x301a60
BUILDER=0x1585320
IARKPLAYER_VTABLE=0x1e56eb8

def header_constant(name):
    """`inline constexpr ... name=0x...` from WorldUiNative.h (comments skipped)."""
    text=(ROOT/'src/aim/WorldUiNative.h').read_text(encoding='utf-8')
    for line in text.splitlines():
        if line.lstrip().startswith('inline constexpr'):
            for found,value in re.findall(r'\b(\w+)=(0x[0-9A-Fa-f]+|\d+)\b',line):
                if found==name:return int(value,0)
    raise KeyError(name)

def verify():
    image=Image(DEFAULT_MODULE);at=image.at
    entries=native_rows(ROOT/'src/aim/WorldUiNative.h')
    assert [rva for rva,_,_,_ in entries]==[CURSOR_RAY],entries
    relocs=image.relocations();checks=[]
    for rva,size,expected,name in entries:
        b=at(rva,size)
        assert fnv(b)==expected,name
        assert not any(rva<=p<rva+size for p in relocs),name+' relocates in memory'
        corrupt=bytearray(b);corrupt[len(b)//2]^=1
        assert fnv(corrupt)!=expected,name+' mutation control'
        assert at(rva+size,1)==b'\xcc',name+' is the whole function'
        checks.append(name+': complete body (padding after it), no base relocations, mutation refused')
    for header in sorted((ROOT/'src').rglob('*Native.h')):
        if header.name!='WorldUiNative.h':assert CURSOR_RAY not in {rva for rva,_,_,_ in native_rows(header)},header.name
    checks.append('no row in another *Native.h table')
    for name,value in {'CursorRay':CURSOR_RAY,'RayReadReturn':READ_RETURN}.items():
        assert header_constant(name)==value,name
    checks.append("WorldUiNative.h's constants are the ones witnessed below")

    def witness(group,table):
        for name,(rva,code) in table.items():
            b=bytes.fromhex(code);assert at(rva,len(b))==b,group+': '+name;checks.append(group+': '+name)

    # The cursor raycast asks the game for the IArkPlayer, reads the ray through its slot 0, and casts it.
    witness('cursor raycast 0x3017C0',{
        'gEnv->pGame (+0xA28)':(0x3017e1,'488b0d40c2f401'),
        'its vtable +0x198 (the IArkPlayer)':(0x3017eb,'ff9098010000'),
        'kept in RBX':(0x3017f1,'488bd8'),
        "the IArkPlayer's vtable":(0x301904,'488b03'),
        'RDX: the 24-byte ray buffer at RSP+0x20':(0x301907,'488d542420'),
        'RCX: the IArkPlayer':(0x30190c,'488bcb'),
        'slot 0, the reticle ray getter':(0x30190f,'ff10'),
        "returning to the read of the element's distance (+0x1E8)":(READ_RETURN,'f30f108fe8010000'),
        'entity types 7':(0x3019bd,'c7442470' '07000000'),
        'flags 0x40F':(0x3019c5,'c7442474' '0f040000'),
        "the hit, if on the element's screen, becomes the cursor":(0x301a26,'e835000000'),
    })
    assert image.rip_target(0x3017e1,7,3)==0x224da28
    checks.append('cursor raycast: RCX is the global at 0x224DA28 (gEnv->pGame)')
    assert image.cstring(image.rip_target(0x301958,7,3))=='RayWorldIntersection(Action)'
    checks.append("cursor raycast: tagged 'RayWorldIntersection(Action)'")
    assert image.call_target(0x301a26)==HIT
    checks.append('cursor raycast: the hit goes to 0x301A60 (into the screen\'s local bounds, pixels at +0x200/+0x204)')

    # The getter is IArkPlayer slot 0 and copies the player's cached ray (player+0x40+0x1794 = +0x17D4).
    assert image.vtable_target(IARKPLAYER_VTABLE)==GETTER
    witness('getter 0x157CBB0',{
        'origin from +0x1794 of player+0x40 (the cached ray at +0x17D4)':(GETTER,'8b8194170000'),
        'into the buffer RDX':(GETTER+6,'8902'),
        'and returns it':(0x157cbe5,'488bc2c3'),
    })
    checks.append('IArkPlayer vtable 0x1E56EB8 slot 0 is the getter 0x157CBB0')
    # Cross-check of the receiver: slots 1 and 2 are one-byte setters, as the other pGame +0x198 callers use them.
    assert image.vtable_target(IARKPLAYER_VTABLE+8)==0x157bb20 and image.vtable_target(IARKPLAYER_VTABLE+16)==0x157d390
    witness('IArkPlayer slots 1 and 2',{
        'slot 1 stores DL at +0x1848':(0x157bb20,'889148180000c3'),
        'slot 2 stores DL at +0x954':(0x157d390,'889154090000c3'),
    })

    # While an examination is on, the ray is built from its cursor (examination +0x60 = player +0xA10).
    witness('ray builder 0x1585320',{
        'the examination is on (+0x9B0+0x98 == 1)':(0x158536c,'83bf480a000001'),
        "the crosshair's screen point (+0x17EC)":(0x158539a,'b8ec170000'),
        "the examination cursor's (+0xA10)":(0x15853a8,'bb100a0000'),
        'the crosshair unless the examination is on':(0x15853b6,'0f45d8'),
    })
    return {'target':str(DEFAULT_MODULE),'sha256':image.sha256,'environment':'static','game_launched':False,'checks':checks,
        'limits':'Proves native bytes and call routes. That pGame +0x198 returns the player\'s IArkPlayer rests on the '
                 'call shape and the matching slots (docs/WORLD-UI-POINTER-2026-10-26.md). Not exercised: the cursor in '
                 'a running game; the screenAim report reads the world UI\'s reads live.'}

if __name__=='__main__':
    report=json.dumps(verify(),indent=2)+'\n'
    if len(sys.argv)>1:Path(sys.argv[1]).write_text(report,encoding='utf-8',newline='\n')
    print(report)
