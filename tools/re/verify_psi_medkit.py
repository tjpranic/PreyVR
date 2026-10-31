"""Read-only Steam psi targeting and medkit ABI witnesses. No process attach."""
import json,struct,sys
from pathlib import Path
from preydll import DEFAULT_MODULE,Image,fnv1a64 as fnv,native_rows
ROOT=Path(__file__).resolve().parents[2]

def verify():
    image=Image(DEFAULT_MODULE);digest,at=image.sha256,image.at
    entries=native_rows(ROOT/'src/equipment/psi/PsiMedkitNative.h')
    assert len(entries)==18
    relocs=image.relocations()
    checks=[]
    for rva,size,expected,name in entries:
        b=at(rva,size)
        assert fnv(b)==expected,name
        assert not any(rva<=p<rva+size for p in relocs),name+' has a base relocation'
        for pos in {0,size//2,size-1}:
            corrupt=bytearray(b);corrupt[pos]^=1
            assert fnv(corrupt)!=expected,name+' mutation control'
        checks.append(f'{name}: {size} bytes; full-body hash, no relocations, 3 mutation controls')
    # Complete bodies include chained/split unwind fragments. In particular,
    # Update=999, Activate=1020, FocusStart=266; first unwind entries are shorter.
    witnesses={
        'IArkPlayer reticle vtable slot zero':(0x1e56eb8,struct.pack('<Q',0x18157cbb0)),
        'reticle origin receiver +1794':(0x157cbb0,bytes.fromhex('8b8194170000')),
        'reticle direction receiver +17a0':(0x157cbca,bytes.fromhex('8b81a0170000')),
        'reticle returns output pointer':(0x157cbe5,bytes.fromhex('488bc2c3')),
        'player psi secondary +678':(0x157cb84,bytes.fromhex('4881c178060000')),
        'embedded power is psi owner +8':(0x12c3370,bytes.fromhex('488d4108c3')),
        'focus secondary member +118':(0x1587c30,bytes.fromhex('488b8118010000c3')),
        'area targeting player +40 getter':(0x15afe86,bytes.fromhex('488d484041ff10')),
        'individual targeting player +40 getter':(0x15bde29,bytes.fromhex('488d4840488b4040ff10')),
        'HUD-preferred argument read from R8B':(0x15bda62,bytes.fromhex('410fb6d8')),
        'preferred candidate skips angular test':(0x15bdad4,bytes.fromhex('84db0f8594000000')),
        'candidate ray-direction angular dot product':(0x15bdb38,bytes.fromhex('f30f595910f30f59610cf30f596914')),
        'native inventory secondary receiver +50':(0x1381b13,bytes.fromhex('488b41504883c150')),
        'native item CanConsume virtual':(0x1381b3a,bytes.fromhex('ff90a0010000')),
        'native item UseFromInventory virtual':(0x1381b4a,bytes.fromhex('ff9090010000')),
        'native consume result 2 after use':(0x1381b50,bytes.fromhex('bb02000000')),
        'native consume result 1 for refusal':(0x1381b57,bytes.fromhex('bb01000000')),
    }
    for name,(rva,b) in witnesses.items():assert at(rva,len(b))==b,name;checks.append(name)
    calls=[('native Start(false)',0x124da62,0x124de50),
        ('native Select(equipped)',0x124da78,0x15b8e00),('native selection refusal Stop',0x124da8a,0x124df60),
        ('native Activate',0x124dc4f,0x15b5d90),('native Stop after targeting',0x124dc78,0x124df60),
        ('individual preferred candidate',0x15bdeca,0x15bda40),('individual ordinary candidate',0x15bdfaf,0x15bda40)]
    for name,rva,target in calls:
        b=at(rva,5);assert b[0]==0xe8 and rva+5+struct.unpack_from('<i',b,1)[0]==target,name
        checks.append(f'{name}: call {rva:#x} -> {target:#x}')
    assert at(0x124da6f,6)==bytes.fromhex('8b9534020000'),'native equipped ID +234'
    lea=at(0x1381c9f,7);assert lea[:3]==bytes.fromhex('488d15')
    name_rva=0x1381c9f+7+struct.unpack_from('<i',lea,3)[0]
    name=b'ArkPickups.Medical.Medkit\0';assert at(name_rva,len(name))==name
    checks.append('native medkit helper resolves exact ArkPickups.Medical.Medkit archetype')
    return {'target':str(DEFAULT_MODULE),'sha256':digest,'environment':'static','game_launched':False,
        'checks':checks,'limits':'Static ABI/flow only. Native execution and hook installation, all power variants, '
        'save/progression acceptance, visible targeting, gesture feel and headset quality require live testing. '
        'Mocked production dispatch is a separate harness result.'}

if __name__=='__main__':
    report=json.dumps(verify(),indent=2)+'\n'
    if len(sys.argv)>1:Path(sys.argv[1]).write_text(report,encoding='utf-8',newline='\n')
    print(report)
