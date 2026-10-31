"""Read-only Steam scene-query contract verification, with byte-mutation controls."""
import json,struct,sys
from pathlib import Path
from preydll import DEFAULT_MODULE,Image,fnv1a64 as fnv,native_rows
ROOT=Path(__file__).resolve().parents[2]

def verify():
    image=Image(DEFAULT_MODULE);digest,at=image.sha256,image.at
    entries=native_rows(ROOT/'src/physics/contact/SceneQueryNative.h')
    assert len(entries)==7
    checks=[]
    for rva,size,expected,name in entries:
        b=at(rva,size)
        assert fnv(b)==expected,name
        assert fnv(bytes([b[0]^1])+b[1:])!=expected,name+' negative control'
        checks.append(name+': full body matches; one-bit mutation refused')
    assert struct.unpack('<Q',at(0x1d82e70+0x118,8))[0]==0x180ccf250
    checks.append('Concrete world vtable +0x118 points to Steam 0xCCF250')
    def proof(name,rva,expected):
        b=bytes.fromhex(expected);assert at(rva,len(b))==b,name;checks.append(name)
    proof('Constructor installs concrete world vtable',0xb9e7b9,'488d05b0461e014533f6488907')
    proof('Native melee obtains player entity at +0x38',0x13bd677,'488b7838')
    proof('Native melee calls entity physics slot +0x228',0x13bd68b,'41ff9028020000')
    proof('Synchronous caller lock',0xccf945,'f0460fb1acb774570000')
    proof('Axis permutation tables used by indexed grid traversal',0x22614a8,
        '0100000002000000000000000000000002000000000000000100000000000000')
    return dict(target=str(DEFAULT_MODULE),sha256=digest,checks=checks,
        environment='static',game_launched=False,
        limits='Proves native bytes/layout/call route, not query acceptance, hit filtering in a scene, rendered reticle depth or performance.')

if __name__=='__main__':
    report=json.dumps(verify(),indent=2)+'\n'
    if len(sys.argv)>1:Path(sys.argv[1]).write_text(report,encoding='utf-8',newline='\n')
    print(report)
