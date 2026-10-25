"""Read-only Steam fullscreen-video contracts: why a playing fullscreen Bink (the first neuromod install, the ApexVision
clips, the endings) reached only the second eye of a both-eyes frame (the render thread draws it after the scene,
straight through D3D11, neither a 2D image nor a Flash movie), and what the copy into the held first eye relies on
(with a viewport of another size and aspect than the video the draw first clears the whole target to black, on the
engine's own view of it, so the backbuffer then holds the video and nothing of the world; the draw asks GetViewport
for that viewport on the static renderer; the video's size is the movie's Bink handle's first two U32). Never
attaches."""
import json,struct,sys
from pathlib import Path
from preydll import DEFAULT_MODULE,Image,fnv1a64 as fnv,native_rows
ROOT=Path(__file__).resolve().parents[2]
DRAW=0xefa9b0
GET_VIEWPORT=0xf7b4b0
RENDERER=0x2b24e80
COMMAND=0xefa360
CLEAR_COLOUR=0x1c848b0

def branches_to(image,target):
    """Every call or jmp rel32 in .text whose target is `target`."""
    text=image.section('.text');data=image.at(text.rva,text.raw_size);found=[]
    for opcode in (b'\xe8',b'\xe9'):
        i=data.find(opcode)
        while i!=-1:
            if i+5<=len(data) and text.rva+i+5+struct.unpack_from('<i',data,i+1)[0]==target:found.append(text.rva+i)
            i=data.find(opcode,i+1)
    return found

def witness(image,checks,prefix,table):
    for name,(rva,code) in table.items():
        b=bytes.fromhex(code);assert image.at(rva,len(b))==b,name;checks.append(prefix+name)

def jmp_target(image,rva):
    assert image.at(rva,1)==b'\xe9',hex(rva)
    return image.rip_target(rva,5,1)

def verify():
    image=Image(DEFAULT_MODULE);at=image.at
    entries=native_rows(ROOT/'include/preyvr/FullscreenVideoNative.h')
    assert [(rva,name) for rva,_,_,name in entries]==[(DRAW,'FullscreenDraw'),(GET_VIEWPORT,'GetViewport')]
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
        if header.name!='FullscreenVideoNative.h':assert not mine&{rva for rva,_,_,_ in native_rows(header)},header.name
    checks.append('no row in another *Native.h table')

    # How the draw is reached: render command 0x2E, run on the render thread through 0xEFA360.
    witness(image,checks,'0xEFA360: ',{'the backend is the Bink manager +0x10':(COMMAND,'488b4910')})
    assert jmp_target(image,COMMAND+4)==DRAW
    assert branches_to(image,DRAW)==[COMMAND+4],[hex(x) for x in branches_to(image,DRAW)]
    rdata=image.section('.rdata')
    assert at(rdata.rva,rdata.raw_size).find(struct.pack('<Q',image.base+DRAW))==-1
    checks.append('only 0xEFA360 reaches the draw (jmp at 0xEFA364), and no table holds its address')
    assert sorted(branches_to(image,COMMAND))==[0xfea1d5,0xfebd99],[hex(x) for x in branches_to(image,COMMAND)]
    witness(image,checks,'',{
        "the command processor hands it the renderer's Bink manager (+0x8398)":(0xfea1ce,'488b8998830000'),
        'and calls it':(0xfea1d5,'e8'),
        'the queuer keeps the movie (RDX) in RSI':(0xfebd25,'488bf2'),
        'and R8D in EDI':(0xfebd22,'418bf8'),
        'off the render thread it queues command 0x2E':(0xfebd59,'c7002e000000'),
        'with the movie':(0xfebd5f,'48897004'),
        'and R8D':(0xfebd63,'89780c'),
        "on it, the renderer's Bink manager":(0xfebd83,'488b8998830000'),
        'is jumped to directly':(0xfebd99,'e9'),
    })

    # The draw: its arguments, the viewport it asks for, the video's size, and the test that clears and letterboxes.
    witness(image,checks,'the draw: ',{
        'R8D kept in EBP':(0xefa9d1,'418be8'),
        'the movie (RDX) kept in RSI':(0xefa9e0,'488bf2'),
        'the backend (RCX) kept in R14':(0xefa9f2,'4c8bf1'),
        'the viewport asked of the static renderer':(0xefa9f9,'488d0d'),
        'through GetViewport':(0xefaa0c,'e8'),
        "the movie's Bink handle (+0x08)":(0xefaa11,'488b4608'),
        'its Width (+0x00)':(0xefaa20,'8b10'),
        'its Height (+0x04)':(0xefaa22,'8b4804'),
        'the viewport width against Width':(0xefaa2c,'443bc2'),
        'the viewport height against Height':(0xefaa31,'3bc1'),
        'the same size: no letterbox':(0xefaa33,'0f84f1000000'),
        'the viewport height as a float':(0xefaa49,'f30f2ac0'),
        'its width':(0xefaa50,'f3410f2af8'),
        "the video's Width":(0xefaa55,'f30f2af2'),
        "the viewport's aspect, single precision":(0xefaa59,'f30f5ef8'),
        "the video's Height":(0xefaa60,'f30f2ac1'),
        "the video's aspect":(0xefaa64,'f30f5ef0'),
        'compared unordered':(0xefaa68,'0f2efe'),
        'equal or unordered: no letterbox':(0xefaa6b,'0f84af000000'),
        'the clear colour':(0xefaa71,'0f2805'),
        'a colour clear (4)':(0xefaa7d,'ba04000000'),
        'cleared now (EF_ClearTargetsImmediately)':(0xefaa8e,'e8'),
        'the letterboxed viewport set':(0xefab18,'e8'),
        'marked for putting back':(0xefab1d,'40b701'),
        'the state committed':(0xefab31,'e8'),
        'the backend handed the movie':(0xefab3c,'488bd6'),
        'and R8D':(0xefab39,'448bc5'),
        "draws it through its vtable's +0x08":(0xefab42,'ff5008'),
        'the viewport put back when marked':(0xefac40,'7433'),
        'and the draw returns':(0xefac94,'5bc3'),
    })
    for rva in (0xefa9f9,0xefaa82,0xefaaee,0xefab2a,0xefac49,0xefac7b):
        assert image.rip_target(rva,7,3)==RENDERER,hex(rva)
    checks.append('every renderer the draw passes is the static one at 0x2B24E80')
    assert image.call_target(0xefaa0c)==GET_VIEWPORT and image.call_target(0xefaa8e)==0xf0d1f0
    assert image.call_target(0xefab18)==0xf7ea90 and image.call_target(0xefac70)==0xf7ea90
    assert image.call_target(0xefab31)==0xf0dbb0
    checks.append('it calls GetViewport 0xF7B4B0, the clear 0xF0D1F0, SetViewport 0xF7EA90 (twice) and the commit 0xF0DBB0')
    assert image.rip_target(0xefaa71,7,3)==CLEAR_COLOUR and struct.unpack('<4f',at(CLEAR_COLOUR,16))==(0,0,0,1)
    checks.append('the clear colour (0x1C848B0) is opaque black')

    # GetViewport: on the render thread it reads the renderer's +0x94A0, and writes x, y, w, h in turn.
    witness(image,checks,'GetViewport: ',{
        'the render thread object (+0xD90)':(0xf7b4ca,'488b99900d0000'),
        'the render thread id (+0x58)':(0xf7b4eb,'3b4358'),
        'or the main one (+0x54)':(0xf7b4f0,'3b4354'),
        'the viewport there at +0x94A0':(0xf7b502,'b9a0940000'),
        'elsewhere at +0x9458':(0xf7b507,'ba58940000'),
        'x to the second argument':(0xf7b519,'418907'),
        'y to the third':(0xf7b51f,'418906'),
        'width to the fourth':(0xf7b52a,'8906'),
        'height to the fifth, on the stack':(0xf7b539,'8908'),
    })
    witness(image,checks,'the queuer runs it directly only on the same threads: ',{
        'the render thread id (+0x58)':(0xfebd36,'3b4358'),
        'or the main one (+0x54)':(0xfebd3b,'3b4354'),
    })

    # The clear: the whole of the engine's own view of its current target, not whatever is bound.
    witness(image,checks,'',{
        'EF_ClearTargetsImmediately ORs in the immediate flag (0x10)':(0xf0d1fe,'83ca10'),
        'records the clear':(0xf0d209,'e8'),
        'and commits the render targets':(0xf0d216,'e9'),
        "EF_ClearTargetsLater takes the current target's entry (+0xAB20)":(0xf0d2b7,'4c8b8920ab0000'),
        'FX_SetActiveRenderTargets ends in FX_ClearTargets':(0xf11ad1,'e9'),
        'FX_ClearTargets reads the entry (+0xAB20)':(0xf0da4b,'488b8920ab0000'),
        'its clear flags (+0x2C)':(0xf0da52,'448b412c'),
        'a colour clear (4)':(0xf0dab3,'41f6c004'),
        'for each of the four targets':(0xf0dabe,'488d9e20ab0000'),
        "the entry's own view":(0xf0dae3,'498b2e'),
        "and its colour (+0x30)":(0xf0daf3,'4d8d4630'),
        'ClearRenderTargetView (device context +0x190)':(0xf0db00,'ff9090010000'),
    })
    assert image.call_target(0xf0d209)==0xf0d2a0 and jmp_target(image,0xf0d216)==0xf118c0
    assert jmp_target(image,0xf11ad1)==0xf0da40
    checks.append('0xF0D1F0 calls EF_ClearTargetsLater 0xF0D2A0 then FX_SetActiveRenderTargets 0xF118C0, '
                  'which ends in FX_ClearTargets 0xF0DA40')
    return {'target':str(DEFAULT_MODULE),'sha256':image.sha256,'environment':'static','game_launched':False,'checks':checks,
        'limits':'Proves native bytes and call routes. Not exercised: the hook in a running game, which movie the '
                 "flow graph plays fullscreen, or how the video looks in a headset."}

if __name__=='__main__':
    report=json.dumps(verify(),indent=2)+'\n'
    if len(sys.argv)>1:Path(sys.argv[1]).write_text(report,encoding='utf-8',newline='\n')
    print(report)
