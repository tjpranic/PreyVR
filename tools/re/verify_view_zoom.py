"""Read-only Steam zoom contracts: why Prey's zoom narrows the view's FOV (the zoom manager at ArkPlayer +0x1898 feeds one
call in ArkPlayerCamera::UpdateView), and what the zoom lane relies on (that function's body, cl_hfov at cvars +0x08, and
UpdateView writing the FOV at SViewParams +0x30, the field the view hook is given). Never attaches."""
import json,struct,sys
from pathlib import Path
from preydll import DEFAULT_MODULE,Image,fnv1a64 as fnv,native_rows
ROOT=Path(__file__).resolve().parents[2]
UPDATE_VIEW,FINAL_HFOV,PUSH,SCOPE_STEP=0x148b820,0x1490be0,0x1490e40,0x1272f80
GAME_CVARS=0x2c16830

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
    """`inline constexpr ... name=0x...` from ViewZoomNative.h."""
    text=(ROOT/'include/preyvr/ViewZoomNative.h').read_text(encoding='utf-8')
    for part in text.replace(';',',').split(','):
        part=part.strip()
        if part.split('=')[0].split()[-1:]==[name]:return int(part.split('=')[1],0)
    raise KeyError(name)

def verify():
    image=Image(DEFAULT_MODULE);at=image.at
    entries=native_rows(ROOT/'include/preyvr/ViewZoomNative.h')
    assert [rva for rva,_,_,_ in entries]==[FINAL_HFOV],entries
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
        if header.name!='ViewZoomNative.h':assert not mine&{rva for rva,_,_,_ in native_rows(header)},header.name
    checks.append('no row in another *Native.h table')
    for name,value in {'FinalHfov':FINAL_HFOV,'PlayerZoomManager':0x1898,'PlayerCamera':0x12a0,
                       'GameCVars':GAME_CVARS,'CVarsHfov':0x08}.items():
        assert header_constant(name)==value,name
    checks.append("ViewZoomNative.h's constants are the ones witnessed below")

    def witness(group,table):
        for name,(rva,code) in table.items():
            b=bytes.fromhex(code);assert at(rva,len(b))==b,group+': '+name;checks.append(group+': '+name)

    # The final hfov: one call in the whole of .text, and no table holds its address.
    assert branches_to(image,FINAL_HFOV)==[0x148c640],[hex(x) for x in branches_to(image,FINAL_HFOV)]
    rdata=image.section('.rdata')
    assert at(rdata.rva,rdata.raw_size).find(struct.pack('<Q',image.base+FINAL_HFOV))==-1
    checks.append('one call reaches the final hfov (0x148C640), and no table holds its address')
    witness('final hfov 0x1490BE0',{
        "the manager's blended value (+0x18)":(0x1490be9,'f30f107118'),
        'less the base the manager starts from':(0x1490bfa,'f30f5cf0'),
        'plus cl_hfov (cvars +0x08)':(0x1490bfe,'f30f587008'),
        'at least 1 degree':(0x1490c03,'f30f5f35'),
    })
    assert image.rip_target(0x1490bf3,7,3)==GAME_CVARS and at(0x1490bf3,3)==bytes.fromhex('488b05')
    assert image.call_target(0x1490bee)==0x11a4b50 and struct.unpack('<f',at(0x1e014b4,4))[0]>85.56
    assert image.rip_target(0x1490c03,8,4)==0x1c7eb28 and struct.unpack('<f',at(0x1c7eb28,4))[0]==1.0
    checks.append('its cvars are the pointer at 0x2C16830, its base the constant 85.57, its floor 1.0')

    # UpdateView, whose RSI is the view's params and R14 the player, as the hook sees them.
    witness('UpdateView 0x148B820',{
        'RSI is the second argument, SViewParams':(0x148b849,'488bf2'),
        'RDI is the first, the camera':(0x148b852,'488bf9'),
        'R14 is the player':(0x148b8e5,'4c8bf0'),
        'the zoom manager is the player +0x1898':(0x148c632,'498d8e98180000'),
        "hfov's tangent over the width":(0x148c65b,'f30f5ec7'),
        'FOV: twice the angle':(0x148c66d,'f30f58c0'),
        'written to SViewParams +0x30':(0x148c6b7,'f30f114630'),
    })
    assert image.call_target(0x148c640)==FINAL_HFOV and image.call_target(0x148b8e0)==0x157c990
    checks.append('UpdateView calls the final hfov, and gets R14 from the local-player getter')

    # cl_hfov: registered as a float at cvars +0x08.
    witness('cl_hfov registration 0x17180E0',{
        'the value is the cvars +0x08':(0x17185d4,'4c8d4708'),
        'the console registers a float':(0x17185f8,'ff5040'),
    })
    assert image.rip_target(0x17185ec,7,3)==0x1e7b6b8 and image.cstring(0x1e7b6b8,16)=='cl_hfov'
    checks.append("the name registered there is 'cl_hfov'")

    # What zooms: the psychoscope's pushes on the same manager, and the scan lock's.
    assert 0x1272dd5 in branches_to(image,PUSH) and 0x1273032 in branches_to(image,PUSH)
    assert 0x15d9989 in branches_to(image,PUSH)
    checks.append('the psychoscope (0x1272DD5, 0x1273032) and the scan lock (0x15D9989) push on the zoom manager')
    witness('scope zoom step 0x1272F80',{'on the same manager':(0x1272fe0,'488d8e98180000')})
    return {'target':str(DEFAULT_MODULE),'sha256':image.sha256,'environment':'static','game_launched':False,'checks':checks,
        'limits':'Proves native bytes and call routes. That the camera UpdateView is given is ArkPlayer +0x12A0 comes from the '
                 'head-tracking hook\'s own contract, and the zoom\'s depths from headset logs (the zooms\' HFOVs are in '
                 'encrypted paks). Not exercised: the lane in a running game, or how a magnified view feels in a headset.'}

if __name__=='__main__':
    report=json.dumps(verify(),indent=2)+'\n'
    if len(sys.argv)>1:Path(sys.argv[1]).write_text(report,encoding='utf-8',newline='\n')
    print(report)
