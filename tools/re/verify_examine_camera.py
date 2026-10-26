"""Read-only Steam examination-camera contracts: why a keypad's camera lands a hand's width from the eyes in the headset
(for a world UI Prey's fit fills the view camera's FOV, and re-runs every frame), and what the Screen Distance hook
relies on (the fit's two callers, both handing it the examination's +0x68; the begin setting the type and entity first;
the examination at ArkPlayer +0x9B0). Never attaches."""
import json,struct,sys
from pathlib import Path
from preydll import DEFAULT_MODULE,Image,fnv1a64 as fnv,native_rows
ROOT=Path(__file__).resolve().parents[2]
BASE=0x180000000
FIT,BEGIN,FRAME=0x15aa660,0x15ab280,0x15abd20

def branches_to(image,target):
    """Every call or jmp rel32 in .text whose target is `target`."""
    text=image.section('.text');data=image.at(text.rva,text.raw_size);found=[]
    for opcode in (b'\xe8',b'\xe9'):
        i=data.find(opcode)
        while i!=-1:
            if i+5<=len(data) and text.rva+i+5+struct.unpack_from('<i',data,i+1)[0]==target:found.append(text.rva+i)
            i=data.find(opcode,i+1)
    return found

def header_constant(name):
    """`inline constexpr ... name=0x...` from ExamineNative.h."""
    text=(ROOT/'include/preyvr/ExamineNative.h').read_text(encoding='utf-8')
    for part in text.replace(';',',').split(','):
        part=part.strip()
        if part.split('=')[0].split()[-1:]==[name]:return int(part.split('=')[1],0)
    raise KeyError(name)

def verify():
    image=Image(DEFAULT_MODULE);at=image.at
    entries=native_rows(ROOT/'include/preyvr/ExamineNative.h')
    assert [rva for rva,_,_,_ in entries]==[FIT,BEGIN,FRAME],entries
    relocs=image.relocations();checks=[]
    for rva,size,expected,name in entries:
        b=at(rva,size)
        assert fnv(b)==expected,name
        assert not any(rva<=p<rva+size for p in relocs),name+' relocates in memory'
        corrupt=bytearray(b);corrupt[len(b)//2]^=1
        assert fnv(corrupt)!=expected,name+' mutation control'
        assert at(rva+size,1)==b'\xcc',name+' is the whole function'
        checks.append(name+': complete body (padding after it), no base relocations, mutation refused')
    mine={rva for rva,_,_,_ in entries}
    for header in sorted((ROOT/'include/preyvr').glob('*Native.h')):
        if header.name!='ExamineNative.h':assert not mine&{rva for rva,_,_,_ in native_rows(header)},header.name
    checks.append('no row in another *Native.h table')
    for name,value in {'FitView':FIT,'BeginReturn':0x15ab5eb,'FrameReturn':0x15abdaa,'PlayerExamination':0x9b0,
                       'ExamTarget':0x68,'ExamViewingBuffer':0x7c,'ExamType':0x94,'ExamState':0x98,
                       'ExamEntity':0x9c,'TypeWorldUi':1}.items():
        assert header_constant(name)==value,name
    checks.append("ExamineNative.h's constants are the ones witnessed below")

    def witness(group,table):
        for name,(rva,code) in table.items():
            b=bytes.fromhex(code);assert at(rva,len(b))==b,group+': '+name;checks.append(group+': '+name)

    # Who examines: ArkPlayerCamera::UpdateView hands the examination (ArkPlayer +0x9B0) to its update.
    witness('UpdateView 0x148B820',{
        'the examination is ArkPlayer +0x9B0':(0x148c1db,'498d8eb0090000'),
        'handed to its update 0x15ABD20':(0x148c1e5,'e836fb1100'),
    })
    assert image.call_target(0x148c1e5)==FRAME

    # The fit: only two callers, both handing it the examination's +0x68 for the camera's position.
    assert branches_to(image,FIT)==[0x15ab5e6,0x15abda5],[hex(x) for x in branches_to(image,FIT)]
    rdata=image.section('.rdata')
    assert at(rdata.rva,rdata.raw_size).find(struct.pack('<Q',BASE+FIT))==-1
    checks.append('two calls reach the fit (0x15AB5E6, 0x15ABDA5), and no table holds its address')
    witness('begin 0x15AB280',{
        'stores the examined entity id (+0x9C)':(0x15ab56c,'89869c000000'),
        'sets the type: 1, a world UI':(0x15ab5a6,'c7869400000001000000'),
        '2, a telescope':(0x15ab5c2,'c7869400000002000000'),
        '3, any other entity':(0x15ab5ce,'c7869400000003000000'),
        'the position is the examination +0x68':(0x15ab5d8,'488d5668'),
        'the facing +0x50':(0x15ab5df,'4c8d4650'),
        'the examination in RCX':(0x15ab5e3,'488bce'),
        'the call':(0x15ab5e6,'e875f0ffff'),
        'and AL tested after it':(0x15ab5eb,'84c0'),
    })
    witness('every frame 0x15ABD20',{
        'a world UI only (type 1)':(0x15abd74,'83be9400000001'),
        'the position is the examination +0x68':(0x15abd9e,'488d5668'),
        'the facing +0x18 or +0x50, added to the examination':(0x15abda2,'4c03c6'),
        'the call':(0x15abda5,'e8b6e8ffff'),
        'returning to the glide (+0x74, its start time)':(0x15abdaa,'0f2f7674'),
        "the range: the selector's interaction distance, ArkPlayer +0xC64":(0x15ac9da,'f30f10b7640c0000'),
    })
    witness('fit 0x15AA660',{
        'reads the type (+0x94)':(0x15aa689,'448b8194000000'),
        'keeps the entity in RDI':(0x15aa690,'498bf9'),
        'and the examination in R15':(0x15aa69f,'4c8bf9'),
        "a world UI: the entity's local bounds (vtable +0x100)":(0x15aaa3d,'ff9000010000'),
        'the view camera (ISystem +0x388)':(0x15aaa8f,'ff9088030000'),
        'kept in RBX':(0x15aaa9e,'488bd8'),
        'viewingBuffer (+0x7C)':(0x15aaaa1,'f3410f10777c'),
        "the camera's vertical FOV (+0x30)":(0x15aaaac,'f30f104030'),
        'times its projection ratio (+0x40)':(0x15aaad5,'f30f594340'),
        "the entity's world matrix (vtable +0xE8)":(0x15aaaef,'ff90e8000000'),
        'success: AL 1':(0x15aadf2,'b001'),
    })
    assert all(image.call_target(rva)==FIT for rva in (0x15ab5e6,0x15abda5))
    # viewingBuffer is the player config's Examination/WorldUI entry, read by the loader into +0x7C.
    witness('loader 0x15AAE40',{'reads viewingBuffer into +0x7C':(0x15aaee4,'4c8d477c')})
    assert image.rip_target(0x15aaee8,7,3)==0x1e59ef0 and image.cstring(0x1e59ef0,32)=='viewingBuffer'
    checks.append("loader 0x15AAE40: the name is 'viewingBuffer'")
    assert struct.unpack('<f',at(0x1c82bf8,4))[0]==-0.5 and struct.unpack('<f',at(0x1c7eb24,4))[0]==0.5
    checks.append("the fit's halves: -0.5 (0x1C82BF8) for the screen's width, 0.5 (0x1C7EB24) for its height and FOV")
    return {'target':str(DEFAULT_MODULE),'sha256':image.sha256,'environment':'static','game_launched':False,'checks':checks,
        'limits':'Proves native bytes and call routes. The fit formula is read from its decompilation '
                 '(docs/KEYPAD-CAMERA-SCOPE-2026-10-26.md); viewingBuffer and the keypads\' bounds come from encrypted '
                 'paks and are reported live by examine.pullback. Not exercised: the hook in a running game, or how '
                 'a keypad looks in a headset.'}

if __name__=='__main__':
    report=json.dumps(verify(),indent=2)+'\n'
    if len(sys.argv)>1:Path(sys.argv[1]).write_text(report,encoding='utf-8',newline='\n')
    print(report)
