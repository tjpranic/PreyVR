"""Read-only Steam starfield contracts: why each star sits 1 m from the head in a both-eyes frame (the space sky's render
object placed at the camera the frame is culled from; stars.dat read as unit vectors with w = 1), and what the hook
relies on (the ArkStars draw reads four fields of its element, every one of them a read, and the star block's
StarRotation). Never attaches."""
import json,struct,sys
from pathlib import Path
from preydll import DEFAULT_MODULE,Image,fnv1a64 as fnv,native_rows
ROOT=Path(__file__).resolve().parents[2]
BASE=0x180000000
DRAW=0xefb4b0

def rip_refs(image,rva,size,target):
    """Offsets in [rva, rva+size) whose rel32 ends an instruction pointing at `target` (no immediate after it)."""
    body=image.at(rva,size)
    return [rva+i for i in range(size-4) if rva+i+4+struct.unpack_from('<i',body,i)[0]==target]

def branches_to(image,target):
    """Every call or jmp rel32 in .text whose target is `target`."""
    text=image.section('.text');data=image.at(text.rva,text.raw_size);found=[]
    for opcode in (b'\xe8',b'\xe9'):
        i=data.find(opcode)
        while i!=-1:
            if i+5<=len(data) and text.rva+i+5+struct.unpack_from('<i',data,i+1)[0]==target:found.append(text.rva+i)
            i=data.find(opcode,i+1)
    return found

def verify():
    image=Image(DEFAULT_MODULE);at=image.at
    entries=native_rows(ROOT/'include/preyvr/StarfieldNative.h')
    assert len(entries)==1
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
        if header.name!='StarfieldNative.h':assert not mine&{rva for rva,_,_,_ in native_rows(header)},header.name
    checks.append('no row in another *Native.h table')
    _,size,_,_=entries[0]

    # The ArkStars draw (0xEFB4B0): what it reads of its element and of the star block.
    witnesses={
        'the draw reads the star mesh (+0x48)':(0xefb4c1,'488b7148'),
        'keeps the element in RDI':(0xefb4c5,'488bf9'),
        'draws nothing without a mesh':(0xefb4c8,'4885f60f84'),
        'reads the star block (+0x40)':(0xefb4d1,'4c8b4140'),
        "the block's size (+0x30)":(0xefb4e1,'f3410f104030'),
        "StarRotation's first float (+0x08)":(0xefb4f3,'f3410f107008'),
        'and its ninth (+0x28)':(0xefb55c,'f3450f107028'),
        'the intensity (+0x2C)':(0xefb575,'f3450f10782c'),
        'the share of stars drawn (+0x34)':(0xefb592,'f3410f594834'),
        "the falloff texture's id (+0x50)":(0xefb5af,'8b4950'),
        'and +0x54, for its sampler':(0xefb82f,'8b4f54'),
        "StarRotation's rows from XMM6 on":(0xefb681,'f30f11742458'),
        "a row's w is 0":(0xefb6c7,'44896c2464'),
        'set as three float4 rows':(0xefb6db,'41b903000000'),
        'from the rows on the stack':(0xefb6e1,'4c8d442458'),
    }
    for name,(rva,code) in witnesses.items():
        b=bytes.fromhex(code);assert at(rva,len(b))==b,name;checks.append(name)
    # The nine StarRotation reads, +0x08..+0x28 in order, into XMM6..XMM14 (movss xmm,[r8+d8]).
    reads=[(0xefb4f3,6,0x08),(0xefb4fd,7,0x0c),(0xefb508,8,0x10),(0xefb516,9,0x14),(0xefb524,10,0x18),
           (0xefb532,11,0x1c),(0xefb540,12,0x20),(0xefb54e,13,0x24),(0xefb55c,14,0x28)]
    for rva,xmm,offset in reads:
        assert at(rva,6)==bytes([0xf3,0x41 if xmm<8 else 0x45,0x0f,0x10,0x40|((xmm&7)<<3),offset]),hex(rva)
    checks.append('the nine StarRotation floats, +0x08..+0x28 in order, into XMM6..XMM14')
    assert image.call_target(0xefb6f0)==0xf467b0
    names=set()
    body=at(DRAW,size)
    for i in range(size-7):
        # lea r64,[rip+disp32]: REX.W, 8D, ModRM with mod 00 and r/m 101.
        if body[i] in (0x48,0x4c) and body[i+1]==0x8d and body[i+2]&0xc7==0x05:
            target=DRAW+i+7+struct.unpack_from('<i',body,i+3)[0]
            try:names.add(image.cstring(target,32))
            except (UnicodeDecodeError,ValueError):pass
    assert {'StarRotation','StarSizeIntensity','ArkStars','Stars'}<=names,names
    checks.append("the draw sets the shader's StarRotation (three float4 rows, w 0) through 0xF467B0, and names "
                  'StarSizeIntensity, the technique ArkStars and the shader Stars')
    # The element is in RCX until the first call (0xEFB5BD), and in RDI (callee-saved) for the whole body. Every
    # mov/lea through either with a byte displacement (ModRM mod 01, r/m 001 or 111; no REX.B) is one of the four
    # reads above, and nothing copies RDI into another register or takes an address from it.
    def through(start,end,rm):
        found=[]
        for i in range(start-DRAW,end-DRAW-2):
            rex=body[i-1] if i and 0x40<=body[i-1]<=0x4f else None
            if body[i] in (0x8b,0x8d) and body[i+1]&0xc7==0x40|rm and (rex is None or not rex&1):
                found.append((DRAW+i-(rex is not None),body[i+2]))
        return found
    rcx=through(DRAW,0xefb5bd,1);rdi=through(DRAW,DRAW+size,7)
    assert sorted(d for _,d in rcx)==[0x40,0x48,0x50],[(hex(a),hex(d)) for a,d in rcx]
    assert [d for _,d in rdi]==[0x54],[(hex(a),hex(d)) for a,d in rdi]
    for code in ('488bcf','488bd7','4c8bc7','4c8bcf','488bc7','488bdf','488bf7','4c8bf7','4c8bff',
                 '4889f9','4889fa','4989f8','4989f9','4889f8','488d0f','488d17'):
        assert bytes.fromhex(code) not in body,code
    assert body.count(bytes.fromhex('488bf9'))==1
    checks.append('the element is read at +0x40, +0x48, +0x50 and +0x54 only, kept in RDI once, and never handed on '
                  '(no register copy of RDI, no address taken from it)')

    # Its only caller: the space sky element's draw, reached through its vtable.
    assert branches_to(image,DRAW)==[0xefbe76],[hex(x) for x in branches_to(image,DRAW)]
    rdata=image.section('.rdata')
    assert at(rdata.rva,rdata.raw_size).find(struct.pack('<Q',BASE+DRAW))==-1
    checks.append('one call reaches the draw (0xEFBE76), and no table holds its address')
    for name,(rva,code) in {
        'the element draw keeps its element in RBX':(0xefbd9d,'488bd9'),
        'tests the batch flag 8':(0xefbe6f,'a8087510'),
        'and without it hands the element to the stars':(0xefbe73,'488bcb'),
        'then to the sun':(0xefbe7b,'488bcb'),
    }.items():
        b=bytes.fromhex(code);assert at(rva,len(b))==b,name;checks.append(name)
    assert image.call_target(0xefbe76)==DRAW and image.call_target(0xefbe7e)==0xefba40
    assert image.u64(0x1db9130)==BASE+0xefbd90
    checks.append("the element's vtable holds its draw at 0x1DB9130; the draw calls the stars 0xEFB4B0, then the sun "
                  '0xEFBA40')

    # The space sky's submit (0xBAC90): the element and its block, and the render object at the culling camera.
    for name,(rva,code) in {
        'the block is the space sky +0x170':(0xbacf2,'498d9c2470010000'),
        'the element is the engine +0x378':(0xbad07,'488b8178030000'),
        'the block is stored at the element +0x40':(0xbad7d,'48895840'),
        'the render object from the renderer +0x440':(0xbacce,'ff9040040000'),
        "the pass's camera (+0x18)":(0xbae4e,'498b4518'),
        'its position x':(0xbae65,'f3440f10580c'),
        'y':(0xbae6b,'f3440f10501c'),
        'z':(0xbae71,'f3440f10602c'),
        "the render object's position: the camera's, x":(0xbaeaa,'f3450f115f0c'),
        'y ':(0xbaeb0,'f3450f11571c'),
        'z ':(0xbaeb6,'f3450f11672c'),
        'sorted last (100000)':(0xbaecc,'41c747540050c347'),
        'the element added with that object (renderer +0x460)':(0xbb1cc,'41ff9660040000'),
    }.items():
        b=bytes.fromhex(code);assert at(rva,len(b))==b,name;checks.append('submit: '+name.strip())
    # The block's rotation, rebuilt on the main thread (0xBB720): space sky +0x178..+0x198 = block +0x08..+0x28.
    stores=[(0xbbac7,0x178),(0xbbb34,0x17c),(0xbbb1f,0x180),(0xbbaff,0x184),(0xbbb5c,0x188),(0xbbb7f,0x18c),
            (0xbbae4,0x190),(0xbbb87,0x194),(0xbbb64,0x198)]
    for rva,offset in stores:
        # movss [rdi+disp32],xmm: F3 [REX] 0F 11 ModRM(mod 10, r/m 111) disp32.
        b=at(rva,9);rex=b[1] if 0x40<=b[1]<=0x4f else None;op=2 if rex is not None else 1
        assert b[0]==0xf3 and b[op:op+2]==b'\x0f\x11' and b[op+2]&0xc7==0x87,hex(rva)
        assert struct.unpack_from('<I',b,op+3)[0]==offset,hex(rva)
    checks.append('the rotation is the space sky +0x178..+0x198, rebuilt by 0xBB720: the block +0x08..+0x28')
    # RenderSkyBox (0x21F2D0) runs the space sky when the HDR sky is not up.
    assert image.call_target(0x21f30f)==0xbb720
    assert any(0x21f2d0<=x<0x21f2d0+590 for x in branches_to(image,0xbac90))
    checks.append('RenderSkyBox 0x21F2D0 rebuilds (0xBB720) and submits (0xBAC90) the space sky')

    # stars.dat: unit vectors, six vertices each, written with w = 1.
    assert image.cstring(0x1db9188,32)=='Libs/Sky/stars.dat' and rip_refs(image,0x1002ab0,1628,0x1db9188)
    for name,(rva,code) in {
        "the magic 'STAR'":(0x1002c12,'817d8853544152'),
        'version 0x10001':(0x1002c63,'817d8c01000100'),
        "the direction's x":(0x1002ec6,'f30f1145a0'),
        'y  ':(0x1002ed7,'f30f1145a4'),
        'z  ':(0x1002f04,'f30f1145a8'),
        'handed to the half-float writer':(0x1002f38,'488d55a0'),
        'vertices 16 bytes apart':(0x1002f34,'48c1e304'),
        'six to a star':(0x1002f4d,'83fe067cde'),
    }.items():
        b=bytes.fromhex(code);assert at(rva,len(b))==b,name;checks.append('stars.dat: '+name.strip())
    assert image.call_target(0x1002f42)==0xf88b0
    for name,(rva,code) in {
        'x':(0xf8946,'6645890a'),'y':(0xf89b7,'6645894a02'),'z':(0xf8a31,'6645894a04'),'w':(0xf8aa1,'6645894a06'),
        'w from the constant':(0xf8a25,'f30f1005'),
    }.items():
        b=bytes.fromhex(code);assert at(rva,len(b))==b,name
    assert image.rip_target(0xf8a25,8,4)==0x1c7eb28 and struct.unpack('<f',at(0x1c7eb28,4))[0]==1.0
    checks.append('the half-float writer 0xF88B0 stores x, y, z and w = 1.0 (0x1C7EB28): the translation applies')
    assert struct.unpack('<f',at(0x1c7fa28,4))[0]==1000.0
    checks.append("Earth and Moon distances are scaled by 1000.0 (0x1C7FA28): kilometres, far enough already")
    return {'target':str(DEFAULT_MODULE),'sha256':image.sha256,'environment':'static','game_launched':False,'checks':checks,
        'limits':'Proves native bytes and call routes. The shaders (Stars.cfx ArkStarsVS, _pos_Common in ModificatorVT.cfi) '
                 'are read from Engine/Shaders.pak, not here; the Milky Way mesh size from Objects-part5.pak. Not exercised: '
                 'the hook in a running game, or how the stars look in a headset.'}

if __name__=='__main__':
    report=json.dumps(verify(),indent=2)+'\n'
    if len(sys.argv)>1:Path(sys.argv[1]).write_text(report,encoding='utf-8',newline='\n')
    print(report)
