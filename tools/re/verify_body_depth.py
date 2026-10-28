"""Read-only Steam body-depth contracts: why the player's body is drawn over everything (the near pass's depth slice,
r_DrawNearZRange, and its own 0.03 m projection), and what the hook relies on (one entity slot render decides the near
pass from the slot's ENTITY_SLOT_RENDER_NEAREST bit and its FOB_NEAREST object flag, and draws the character, its
skins and its skeleton attachments inside the call; without the bit the matrix is the slot's world transform; the
main view's pass is the one CSystem::Render builds with rendering flags 0x2E5DF). Never attaches."""
import json,struct,sys
from pathlib import Path
from preydll import DEFAULT_MODULE,Image,fnv1a64 as fnv,native_rows
ROOT=Path(__file__).resolve().parents[2]
SLOT_RENDER=0x973400
SET_VIEWPORT=0xf90390
NEAREST_CHANGE=0xf43d70
CHARACTER_VTABLE=0x1d22200

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

def verify():
    image=Image(DEFAULT_MODULE);at=image.at
    entries=native_rows(ROOT/'include/preyvr/BodyDepthNative.h')
    assert [(rva,name) for rva,_,_,name in entries]==[(SLOT_RENDER,'SlotRender'),(SET_VIEWPORT,'SetViewport'),
                                                       (NEAREST_CHANGE,'NearestChange')]
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
        if header.name!='BodyDepthNative.h':assert not mine&{rva for rva,_,_,_ in native_rows(header)},header.name
    checks.append('no row in another *Native.h table')

    # The slot render (CEntityObject::Render): its arguments, the near decision and the two matrices.
    witness(image,checks,'slot render: ',{
        'six pushes, then rbp = rsp - 0xE8 (so rbp + 0x148 is the sixth argument)':
            (0x973400,'405553565741544157488dac2418ffffff'),
        'the params (R8) kept in RBX':(0x97342a,'498bd8'),
        'the slot (RCX) kept in RSI':(0x973430,'488bf1'),
        "the slot's object flags (+0xA8)":(0x973452,'8b86a8000000'),
        "the params' object flags (+0x80), saved":(0x973458,'4c8ba380000000'),
        'ORed together':(0x973466,'490bc4'),
        'into the params':(0x973471,'48898380000000'),
        'the incoming flags kept at rbp-0x60':(0x97349a,'4c8965a0'),
        "the slot's own flags (+0xAC)":(0x9734af,'440fb6a6ac000000'),
        'bit 1 (ENTITY_SLOT_RENDER_NEAREST):':(0x9734b7,'41d0ec'),
        'the near branch flag':(0x9734c3,'4180e401'),
        "the world matrix, the slot's +0x08":(0x9734ec,'4c8d7608'),
        'is the params matrix':(0x9734f0,'4c8933'),
        'unless the near branch builds a camera-relative one':(0x9735e0,'4584e4'),
        'the pass info, the sixth argument, in R13':(0x97345f,'4c8bad48010000'),
        "the slot's character (+0x80)":(0x9747d6,'488b8e80000000'),
        'handed the pass info in R9':(0x9747e8,'4d8bcd'),
        "drawn through its vtable's +0xC0":(0x9747f6,'ff90c0000000'),
        "the params' flags put back":(0x9748a3,'488b45a0'),
        'before it returns':(0x9748a7,'48898380000000'),
    })
    assert image.vtable_target(CHARACTER_VTABLE+0xc0)==0x81bcb0
    assert image.call_target(0x81c6f3)==0x81d0d0 and image.call_target(0x81c707)==0x821fd0
    checks.append("the character's Render (vtable 0x1D22200 +0xC0, 0x81BCB0) calls RenderCHR 0x81D0D0, then its "
                  'attachments 0x821FD0, inside the call')
    assert image.vtable_target(CHARACTER_VTABLE+0x90)==0x82f910 and image.vtable_target(CHARACTER_VTABLE+0xc8)==0x82ec20
    witness(image,checks,'',{"the character's SetFlags (+0xC8) stores its flags at +0xAC8":(0x82ec20,'8991c80a0000')})

    # Its only caller: the entity's draw loop.
    assert branches_to(image,SLOT_RENDER)==[0x97cf2f],[hex(x) for x in branches_to(image,SLOT_RENDER)]
    rdata=image.section('.rdata')
    assert at(rdata.rva,rdata.raw_size).find(struct.pack('<Q',image.base+SLOT_RENDER))==-1
    checks.append('one call reaches the slot render (0x97CF2F), and no table holds its address')
    witness(image,checks,'the draw loop: ',{
        'the params on its stack':(0x97cf15,'4c8d442430'),
        'the slot':(0x97cf1e,'498bce'),
        'the pass info, the sixth argument':(0x97cf25,'4889442428'),
        'the render proxy, the fifth':(0x97cf2a,'4889742420'),
    })

    # Near-ness downstream, read inside the call.
    witness(image,checks,'',{
        "RenderCHR reads the params' flags":(0x81d127,'8b8680000000'),
        'tests FOB_NEAREST (bit 23)':(0x81d13a,'480fbae017'),
        "or the character's CS_FLAG_DRAW_NEAR":(0x81d141,'f687c80a000002'),
        'clears the render object bit without either':(0x81d14a,'480fbaf117'),
        'and sets it with one':(0x81d155,'480fbae917'),
        "a skin attachment tests the master's CS_FLAG_DRAW_NEAR":(0x7aae06,'f681c80a000002'),
        'sets FOB_NEAREST with it':(0x7aae0f,'480fbaeb17'),
        'clears it without':(0x7aae16,'480fbaf317'),
        "and ORs the params' flags":(0x7aae49,'480b9e80000000'),
        "a skeleton attachment (the weapon) tests the master's CS_FLAG_DRAW_NEAR":(0x7c768a,'f681c80a000002'),
        'the skeleton attachment sets FOB_NEAREST with it':(0x7c7693,'480fbaeb17'),
        'and clears it without':(0x7c769a,'480fbaf317'),
        "and ORs the params' flags too":(0x7c76cd,'490b9f80000000'),
    })

    # Every slot update derives the object flag from the slot's bit.
    witness(image,checks,'slot update 0x9763F0: ',{
        'stores the slot flags':(0x97647c,'668982ac000000'),
        'tests bit 1':(0x976483,'f6c102'),
        'sets FOB_NEAREST on +0xA8':(0x976488,'818aa800000000008000'),
        'or clears it':(0x976494,'81a2a8000000ffff7fff'),
    })

    # The main view's pass: CSystem::Render's flags, stored by the builder at +0x04.
    witness(image,checks,'',{
        'CSystem::Render builds its pass with rendering flags 0x2E5DF':(0xe0bc2b,'41b8dfe50200'),
        'CreateGeneralPassRenderingInfo stores them at +0x04':(0x1e5bf9,'897e04'),
    })
    calls=[i for i in range(0xe0bc31,0xe0bc70) if at(i,1)==b'\xe8' and image.call_target(i)==0x1e5b30]
    assert calls,'CSystem::Render calls the builder'
    checks.append('with that constant CSystem::Render calls CreateGeneralPassRenderingInfo 0x1E5B30')

    # Why the body is drawn over everything: the near pass's depth slice and projection.
    assert image.rip_target(0xecd247,7,3)==0x2b1c718 and image.rip_target(0xecd253,7,3)==0x1dc6898
    assert image.cstring(0x1dc6898)=='r_DrawNearZRange' and image.cstring(0x1dc6888)=='Default is 0.1.'
    checks.append('r_DrawNearZRange is registered at 0x2B1C718, default 0.1')
    witness(image,checks,'',{
        'UpdateNearestChange reads r_DrawNearZRange for the viewport':(0xf43f52,'f30f1005be87bd01'),
        'and marks the pipeline nearest (0x10000)':(0xf43f5a,'818b8848000000000100'),
        'the near viewport: min depth 0':(0xf903c0,'c7415000000000'),
        'max depth r_DrawNearZRange':(0xf903c7,'f30f100549c3b801'),
        'stored as the max depth':(0xf903cf,'f30f114154'),
        'flipped to [1 - range, 1] with reverse depth':(0xf903d4,'a802'),
    })
    assert image.rip_target(0xf43f52,8,4)==0x2b1c718 and image.rip_target(0xf903c7,8,4)==0x2b1c718
    assert abs(struct.unpack('<f',at(0x1c7f778,4))[0]-0.03)<1e-7
    checks.append('both read 0x2B1C718; the near projection near plane is 0.03 (0x1C7F778)')

    # body.depth 1, the near pass at the world's depth: the slice's two writers, hooked.
    witness(image,checks,'SetViewport 0xF90390: ',{
        "the viewport (RDX) copied to the pass (RCX) at +0x40":(0xf90394,'0f1002'),
        "the viewport's depth (+0x10)":(0xf903a1,'f20f104a10'),
        "stored as the pass's depth (+0x50)":(0xf903a6,'f20f114950'),
        'replaced with the slice when the flags (+0xAC, read into EAX) carry bit 0':(0xf903bc,'a801'),
        'its max depth at +0x54':(0xf903cf,'f30f114154'),
    })
    assert at(SET_VIEWPORT+7,6)==bytes.fromhex('8b81ac000000'),'the flags are the pass +0xAC'
    witness(image,checks,'the scene stage 0xF9DAE0: ',{
        'the reverse-depth bit (bit 1)':(0xf9de22,'03ff'),
        'with bit 0, the near slice':(0xf9de3c,'83cb01'),
        "a near pass's flags":(0xf9de42,'899edc000000'),
        'then its viewport set':(0xf9de48,'e84325ffff'),
        'two more near passes':(0xf9de52,'899ea4010000'),
        'and the third':(0xf9de65,'899e6c020000'),
        'a pass without the bit':(0xf9de78,'89be34030000'),
    })
    callers=[a for a in branches_to(image,SET_VIEWPORT)]
    assert callers==[0xf85d6f,0xf9de48,0xf9de5b,0xf9de6e,0xf9de85,0xf9ded4,0xfa4a7b],[hex(a) for a in callers]
    checks.append('seven calls reach SetViewport; the scene stage sets bit 0 for the three near passes it sets')
    witness(image,checks,'UpdateNearestChange 0xF43D70: ',{
        'the renderer (RCX) in RBX':(0xf43d89,'488bd9'),
        'entering on EDX bit 16':(0xf43d99,'0fbae210'),
        "the renderer's camera saved at +0x5620":(0xf43ddc,'488d8b20560000'),
        'the slice as the pending max depth (+0x94B4)':(0xf43f8f,'f30f1183b4940000'),
        'leaving, the saved camera put back':(0xf43faf,'488d9320560000'),
        "and its own max depth (+0x5820, CCamera zRangeMax 0x200)":(0xf43fbe,'8b8320580000'),
        'the renderer no longer near':(0xf43fc4,'81a388480000fffffeff'),
        'as the pending max depth':(0xf43fce,'8983b4940000'),
        'either way marked dirty (+0x94B8)':(0xf43fd4,'c683b894000001'),
    })
    assert branches_to(image,NEAREST_CHANGE)==[0xf39eb8]
    witness(image,checks,'FX_ObjectChange 0xF39E00: ',{
        "the object's flags":(0xf39e93,'418b4940'),
        "FOB_NEAREST (bit 23) shifted to bit 16":(0xf39e9d,'48c1e907'),
        'masked':(0xf39ea1,'81e100000100'),
        'and handed to UpdateNearestChange, its one caller':(0xf39eb8,'e8b39e0000'),
    })
    witness(image,checks,'the pending viewport applied (0xF12D90): ',{
        'when dirty':(0xf12d94,'80b9b894000000'),
        'with its max depth':(0xf12df4,'f30f1081b4940000'),
    })
    # The packer: view-info matrices to the per-view payload.
    witness(image,checks,'the per-view packer 0xFB57A0: ',{
        'the view-info (RDX) in RSI':(0xfb57cf,'488bf2'),
        'the translation-free world VP (+0x20)':(0xfb5970,'f30f104620'),
        'to payload +0x00':(0xfb59c8,'f30f1107'),
        'the world VP (+0x60)':(0xfb5a1f,'f30f104660'),
        'to payload +0x50':(0xfb5ae9,'f30f114750'),
        'the near VP (+0xA0)':(0xfb5af3,'f30f1086a0000000'),
        'to payload +0x90':(0xfb5b7b,'f30f118790000000'),
    })
    return {'target':str(DEFAULT_MODULE),'sha256':image.sha256,'environment':'static','game_launched':False,'checks':checks,
        'limits':'Proves native bytes and call routes. Not exercised: the hooks in a running game, which passes reach the '
                 "player's slot with which flags, which near passes the main view draws, or how the body looks, is lit "
                 'and is hidden in a headset.'}

if __name__=='__main__':
    report=json.dumps(verify(),indent=2)+'\n'
    if len(sys.argv)>1:Path(sys.argv[1]).write_text(report,encoding='utf-8',newline='\n')
    print(report)
