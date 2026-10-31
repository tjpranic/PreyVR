"""Read-only Steam physical-melee/rigid-action ABI witnesses. Never attaches."""
import json,struct,sys
from pathlib import Path
from preydll import DEFAULT_MODULE,Image,fnv1a64 as fnv,native_rows
ROOT=Path(__file__).resolve().parents[2]

def verify():
    image=Image(DEFAULT_MODULE);digest,at=image.sha256,image.at
    entries=native_rows(ROOT/'src/physics/contact/PhysicalNative.h')
    assert len(entries)==15
    relocs=image.relocations()
    checks=[]
    for rva,size,expected,name in entries:
        b=at(rva,size)
        assert fnv(b)==expected,name
        assert not any(rva<=p<rva+size for p in relocs),name+' relocates in memory'
        corrupt=bytearray(b);corrupt[len(b)//2]^=1
        assert fnv(corrupt)!=expected,name+' mutation control'
        checks.append(name+': complete body, no base relocations, mutation refused')
    for table,slot,callee in [(0x1d83870,0x38,0xbcf990),(0x1d83870,8,0x13d1b90),(0x1d22200,0x98,0x82dd40)]:
        assert struct.unpack('<Q',at(table+slot,8))[0]==0x180000000+callee
        checks.append(f'vtable {table:#x}+{slot:#x} -> {callee:#x}')
    witnesses={
        'native fatigue admission':(0x16b2050,'e88bf8baff'),
        'native idle attack starts animation':(0x16b20af,'e86c0d0000'),
        'weapon owns component at +0x4c8':(0x16b2c16,'4881c1c8040000'),
        'component calls GetHits':(0x13bfa96,'e885dbffff'),
        'component calls weapon impulse':(0x13bfb69,'e8a2e82e00'),
        'component charges native fatigue':(0x13bfed8,'e84314eaff'),
        'bound-character local AABB getter':(0x82dd40,'488d81f0090000c3'),
        'queue pointer-payload metadata':(0xb1b2d0,'4f8b84d3e02d9802'),
        'queue action-size metadata':(0xb1b558,'418bbc83a02c9802'),
        'rigid inverse mass consumed at +0x2e8':(0xbcfaef,'f3440f10a3e8020000'),
        'rigid centre consumed at +0x298':(0xbcfb91,'f30f5ca398020000'),
        'rigid world inverse inertia begins at +0x328':(0xbcfcbb,'f30f598328030000'),
        'rigid world inverse inertia ends at +0x348':(0xbcfd27,'f30f598b48030000'),
    }
    for name,(rva,code) in witnesses.items():
        b=bytes.fromhex(code);assert at(rva,len(b))==b,name;checks.append(name)
    return {'target':str(DEFAULT_MODULE),'sha256':digest,'environment':'static','game_launched':False,'checks':checks,
        'limits':'Native calls, MinHook installation, runtime queue descriptors, contact filtering, performance and headset alignment are not exercised. Mocked native dispatch is a separate offline test.'}

if __name__=='__main__':
    report=json.dumps(verify(),indent=2)+'\n'
    if len(sys.argv)>1:Path(sys.argv[1]).write_text(report,encoding='utf-8',newline='\n')
    print(report)
