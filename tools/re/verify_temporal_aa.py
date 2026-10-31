"""Read-only Steam temporal-AA contracts (docs/TEMPORAL-AA-SCOPE-2026-10-30.md): what a per-eye temporal history
would have to separate. One history pair ($PrevBackBuffer0/1), one previous-camera ring inside the AA stage, and the
jitter sample are all chosen from one global counter that the post-processing driver advances once per run; the
resolve's camera comes from the render view's current (+0x11A0) and previous (+0x13E0) cameras. Moving and
first-person objects' motion vectors are taken against the renderer's previous view per viewport (+0xAB0), which
only the same driver writes, and first-person ones through the near projection alone (view-info +0x260), which
NearViewStereo must move with each eye. Those motion constants compile once for the one render view both eyes draw,
so the world's previous projection (+0x220) must take the second eye from the first eye's view. Never attaches."""
import struct,sys
from pathlib import Path
from preydll import DEFAULT_MODULE,Image,fnv1a64 as fnv,native_rows
ROOT=Path(__file__).resolve().parents[2]
HEADER=ROOT/'src/render/taa/TemporalAaNative.h'

def header_constant(name):
    """`inline constexpr ... name=0x...` from TemporalAaNative.h."""
    text=HEADER.read_text(encoding='utf-8')
    for part in text.replace(';',',').split(','):
        part=part.strip()
        if '=' in part and part.split('=')[0].split()[-1:]==[name]:return int(part.split('=')[1].strip(),0)
    raise KeyError(name)

COUNTER=0x2bb5150                    # SPostEffectsUtils-style frame counter, mod 2^24
HISTORY=(0x2b3f690,0x2b3f698)         # $PrevBackBuffer0, $PrevBackBuffer1
VELOCITY=0x2b3f7f0                    # $VelocityObjects
MODE_CVAR,MODE_EFFECTIVE=0x2b1c74c,0x2b1c750
PIPELINE=0x2b2fc50
PATTERN=0x2b1c758                     # r_AntialiasingTAAPattern
CREATE_RT=0x100ae50
POST_DRIVER,RESOLVE,SPATIAL,DISPATCH,VIEW_INFO=0xf85230,0xf99fe0,0xf99500,0xf9c790,0xfb1670
SET_PREVIOUS_CAMERA,RENDER_VIEW_VTABLE=0xee82d0,0x1dcae00
PREV_VIEW_STORE,PREV_VIEW_SLOT,SET_VIEWPORT=0xfe7440,0xfe5560,0xf7ea90
CURRENT_VIEW,PREV_WORLD,MOTION_MATRIX,MOTION_RING=0xfe49e0,0xfd4150,0xfd4570,0x2b321d0
MOTION_THRESHOLD=0x2b1c248            # r_MotionBlurThreshold
NEAR_PROJECTION=0xfb1530
COMPILE_OBJECTS,COMPILE,INSTANCE_CONSTANTS,DRAW_COMPILED=0xee46d0,0xeffcd0,0xf001d0,0xf00a90
RENDERER=0x2b24e80                    # gcpRendD3D, an object in .data
GRAPHICS_PIPELINE=0x2b1c8dc           # r_GraphicsPipeline

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
    image=Image(DEFAULT_MODULE);at=image.at;checks=[]

    def witness(group,table):
        for name,(rva,code) in table.items():
            b=bytes.fromhex(code);assert at(rva,len(b))==b,group+': '+name;checks.append(group+': '+name)

    for rva,text in ((0x1de5098,'$PrevBackBuffer0'),(0x1de50b0,'$PrevBackBuffer1'),(0x1dcce20,'$VelocityObjects'),
                     (0x1dbea38,'r_AntialiasingMode')):
        assert image.cstring(rva)==text,text
    checks.append('the history, velocity and cvar names are where the code below points')

    # Creation: one CTexture* per history slot, so one pair for every view.
    for name_rva,lea,call,store,slot in ((0x1de5098,0x10716fb,0x1071714,0x1071719,HISTORY[0]),
                                         (0x1de50b0,0x10717cd,0x10717e7,0x10717ec,HISTORY[1])):
        assert at(lea,3)==b'\x48\x8d\x0d' and image.rip_target(lea,7,3)==name_rva
        assert image.call_target(call)==CREATE_RT
        assert at(store,3)==b'\x48\x89\x05' and image.rip_target(store,7,3)==slot
    assert image.rip_target(0x100f36e,7,3)==0x1dcce20 and image.call_target(0x100f39e)==0x100b510
    assert at(0x100f3d1,3)==b'\x48\x89\x05' and image.rip_target(0x100f3d1,7,3)==VELOCITY
    checks.append('creation 0x1071510: $PrevBackBuffer0/1 go to 0x2B3F690/0x2B3F698; $VelocityObjects to 0x2B3F7F0')

    # The mode: the cvar's storage, its clamped effective copy, and the flag the stages test.
    assert image.rip_target(0xec8750,7,3)==MODE_CVAR and image.rip_target(0xec875c,7,3)==0x1dbea38
    # 0xECEC10: mov ecx,5; cmp [cvar],ecx; cmovl ecx,[cvar]; mov [cvar],ecx; mov [effective],ecx; ret
    assert at(0xecec10,5)==b'\xb9\x05\x00\x00\x00' and at(0xecec2e,1)==b'\xc3'
    for rva,length,prefix,target in ((0xecec15,6,b'\x39\x0d',MODE_CVAR),(0xecec1b,7,b'\x0f\x4c\x0d',MODE_CVAR),
                                     (0xecec22,6,b'\x89\x0d',MODE_CVAR),(0xecec28,6,b'\x89\x0d',MODE_EFFECTIVE)):
        assert at(rva,len(prefix))==prefix and image.rip_target(rva,length,len(prefix))==target,hex(rva)
    assert image.rip_target(0xec879c,6,2)==MODE_EFFECTIVE and image.rip_target(0xec877c,6,2)==MODE_CVAR
    checks.append('r_AntialiasingMode is 0x2B1C74C; 0x2B1C750 is its copy, clamped to 5 by 0xECEC10')
    witness('mode flags 0xFE3760',{'1 << min(effective mode, 5)':
                                   (0xfe3760,'8b0dea8fb301b8050000003bc80f4fc8b801000000d3e0c3')})
    assert image.rip_target(0xfe3760,6,2)==MODE_EFFECTIVE
    witness('dispatch 0xF9C790',{'flags & 0xE: spatial SMAA (modes 1-3)':(0xf9c810,'f6c30e'),
                                 'flags & 0x3C: the temporal resolve (modes 2-5)':(0xf9c82d,'f6c33c')})
    assert image.call_target(0xf9c823)==SPATIAL and image.call_target(0xf9c842)==RESOLVE
    witness('pipeline 0xFB2A00',{'the AA stage is the pipeline +0x288':(0xfb2a0e,'488b8b88020000')})
    assert image.call_target(0xfb2a15)==DISPATCH
    # The pipeline itself: a thunk loads it from 0x2B2FC50 and jumps to its AA execute; the resolve hands
    # the same global to 0xFB1670, which takes the render view from its +0x2E0.
    assert at(0xfce940,3)==b'\x48\x8b\x0d' and image.rip_target(0xfce940,7,3)==PIPELINE
    assert at(0xfce947,1)==b'\xe9' and image.rip_target(0xfce947,5,1)==0xfb2a00
    witness('view info 0xFB1670',{"the pipeline's render view (+0x2E0)":(0xfb1732,'498b96e0020000')})
    checks.append('the pipeline is 0x2B2FC50 (thunk 0xFCE940 to 0xFB2A00)')

    # The counter: two writers in the whole image, the static initialiser and the post driver's increment.
    text=image.section('.text');code=at(text.rva,text.raw_size);writers,readers=[],[]
    # Every RIP-relative ModRM operand (mod 00, r/m 101) that lands on the counter, for an instruction ending
    # 4, 5 or 8 bytes after its displacement (no, imm8 or imm32 immediate). The opcode sits before the ModRM.
    for p in range(2,len(code)-4):
        modrm=code[p-1]
        if modrm&0xc7!=0x05:continue
        disp=struct.unpack_from('<i',code,p)[0]
        for ext in (4,5,8):
            if text.rva+p+ext+disp!=COUNTER:continue
            op,reg=code[p-2],(modrm>>3)&7
            store=(op in (0x89,0x87) and ext==4) or (op==0xc7 and ext==8) or (op==0xff and reg<2 and ext==4) \
                or (op==0x83 and ext==5 and reg!=7) or (op==0x81 and ext==8 and reg!=7)
            (writers if store else readers).append(text.rva+p-2)
    assert writers==[0x1bc5c,0xf8567e],[hex(w) for w in writers]
    assert len(readers)>=8,[hex(r) for r in readers]
    checks.append('the counter 0x2BB5150 has exactly two writers: 0x1BC5C (static init) and 0xF8567E')
    witness('post driver 0xF85230',{
        'previous view-projection (+0xD30) taken from the current (+0xCF0)':(0xf8559f,'8b81f00c00008981300d0000'),
        'counter + 1, kept in 24 bits':(0xf8565f,'8b05ebfac201ffc025ffffff807d09ffc80d000000ffffc0'),
        'stored back':(0xf8567e,'8905ccfac201')})
    assert image.rip_target(0xf8565f,6,2)==COUNTER and image.rip_target(0xf8567e,6,2)==COUNTER

    # Jitter: the sample is counter mod N, written to the renderer's sub-pixel offset (R-129).
    assert image.rip_target(0xf4206e,6,2)==COUNTER
    witness('jitter 0xF41CA0',{'image base for the tables':(0xf42086,'4c8d0573df0bff'),
                              'two-sample pattern: counter & 1':(0xf420a4,'81e301000080'),
                              'its table at 0x2279A78':(0xf420b6,'f3410f1094c0789a2702'),
                              'the offset to +0xD70 (R-129)':(0xf42299,'f30f1195700d0000')})

    # Which pattern: r_AntialiasingTAAPattern (0x2B1C758). Its value 1 picks by mode: 2 for SMAA 2TX (flag 0x8),
    # 5 for TSAA (flag 0x10). Pattern 5 is the 4-sample rotated grid, counter & 3 (seen live 2026-10-30, PreyVR-105).
    assert image.rip_target(0xec87ba,7,3)==PATTERN and image.rip_target(0xec87d1,7,3)==0x1dbeb38
    assert image.rip_target(0xf4203f,6,2)==PATTERN and image.call_target(0xf4204d)==0xfe3760
    witness('jitter 0xF41CA0',{
        'pattern 1: SMAA 2TX gives 2, TSAA gives 5':
            (0xf42045,'83f8017524488bcde80e170a00a8087407b802000000eb11488bcde8fb160a002410f6d81bc083e005'),
        'pattern 5: counter & 3 into the table at 0x2279AC0':
            (0xf42129,'81e3030000807d07ffcb83cbfcffc34863c3f3410f1094c0c09a2702')})
    assert image.i32(0xf42bd4+3*4)==0xf42129
    assert [struct.unpack_from('<2f',at(0x2279ac0+8*k,8)) for k in range(4)]==\
        [(-0.125,-0.375),(0.375,-0.125),(-0.375,0.125),(0.125,0.375)]
    checks.append('jump table: pattern 5 is that case; its table is the 4-sample rotated grid')

    # The resolve: one counter value chooses the ring slots, the history pair and the freshness test.
    assert image.call_target(0xf9a06c)==VIEW_INFO and image.rip_target(0xf9a071,7,3)==COUNTER
    witness('resolve 0xF99FE0',{
        'counter to R14D, then (counter - 1) & 3':(0xf9a071,'448b35d8b0c101418d4eff81e103000080'),
        'reads ring slot (counter-1)&3 at stage +0xBA8, 0x80 per slot':(0xf9a11e,'f20f108419a80b0000'),
        'writes ring slot counter&3':(0xf9a7a6,'f20f118418a80b0000'),
        'image base':(0xf9a88b,'488d156e5706ff'),
        'output: history[counter & 1]':(0xf9a892,'4c8bbcc290f6b302'),
        'history is used if counter - stage+0x20 < 10':(0xf9a89d,'2b432083f80a'),
        'input: history[(counter+1) & 1]':(0xf9a8a5,'8d41012501000080'),
        'the history array':(0xf9a8b8,'488d8a90f6b302'),
        'stage +0x20 = counter, last':(0xf9bafa,'44897320')})
    assert image.i32(0xf9a896)==HISTORY[0] and image.i32(0xf9a8bb)==HISTORY[0]

    # The view's cameras: the resolve's camera block reads current (+0x11A0) and previous (+0x13E0).
    witness('view info 0xFB1670',{'current camera':(0xfb1754,'488d9aa0110000'),
                                 'previous camera':(0xfb1763,'4c8d8ae0130000')})
    witness('SetPreviousFrameCamera 0xEE82D0',{'the view +0x13E0':(0xee82d0,'4881c1e0130000')})
    assert image.rip_target(0xee82d7,5,1)==0x111c70  # CCamera::operator=
    assert image.vtable_target(RENDER_VIEW_VTABLE+0x48)==SET_PREVIOUS_CAMERA
    checks.append('CRenderView vtable 0x1DCAE00 +0x48 (slot 9) is SetPreviousFrameCamera (R-064)')

    # Motion vectors (the scope's known issue). Which objects write one: CMotionBlur::SetupObject sets
    # FOB_HAS_PREVMATRIX when the object moved past r_MotionBlurThreshold, or always for FOB_MOTION_BLUR|FOB_NEAREST.
    witness('motion 0xED8550',{'its flags; the threshold; always for FOB_MOTION_BLUR|FOB_NEAREST (0xC00000)':
                               (0xed8758,'498b4340f30f1015e43ac40148a90000c000')})
    assert image.rip_target(0xed875c,8,4)==MOTION_THRESHOLD
    # The instance setup builds PI_MotionBlurData: the previous world matrix (0xFD4150, from the ring of 3), then
    # times the renderer's previous view for this viewport (0xFE5560 into 0xFD4570).
    for call,target in ((0xf003aa,PREV_WORLD),(0xf003bd,PREV_VIEW_SLOT),(0xf003cf,MOTION_MATRIX)):
        assert image.call_target(call)==target,hex(call)
    witness('previous world 0xFD4150',{'only with FOB_HAS_PREVMATRIX (bit 30)':(0xfd418c,'480fbae01e0f83d50000'),
                                       'from the ring at 0x2B321D0':(0xfd41c1,'4c8d1d08e0b501')})
    assert image.rip_target(0xfd41c1,7,3)==MOTION_RING
    witness('motion matrix 0xFD4570',{
        "the view's translation row: +0x30":(0xfd45aa,'f3410f107830'),
        "the view's translation row: +0x34, +0x38":(0xfd4630,'f3450f106834f3450f107038'),
        'dropped for FOB_NEAREST (bit 23)':(0xfd46a2,'8b4140480fbae017730b0f57ff450f57ed450f57f6')})
    checks.append('PI_MotionBlurData: previous world x the renderer previous view, translation dropped when nearest')
    # The renderer's previous view per viewport: +0xAB0 + 0x40 x viewport (+0x8060, 0..7).
    witness('previous view slot 0xFE5560',{'+0xAB0 + 0x40 x [+0x8060]':
                                           (PREV_VIEW_SLOT,'4863816080000048c1e0064805b00a00004803c1c3')})
    witness('previous view store 0xFE7440',{'the same slot, written':
                                            (PREV_VIEW_STORE,'4c6381608000008b0249c1e00641898408b00a0000')})
    witness('SetViewport 0xF7EA90',{'the viewport id clamped to 7: 8 slots':(0xf7eb09,'b8070000003bd00f4cc2898160800000c3')})
    # Written only by the post driver, each run, with this run's view: one per submission, so in a both-eyes frame
    # each eye's moving objects take the other eye's view unless it is kept per eye.
    assert branches_to(image,PREV_VIEW_STORE)==[0xf85593],[hex(b) for b in branches_to(image,PREV_VIEW_STORE)]
    assert image.call_target(0xf85584)==CURRENT_VIEW
    witness('post driver 0xF85230',{"this run's view to its viewport's slot, then the view-projection copy":
                                    (0xf85584,'e857f40500488b0d5093bb01488bd0e8a81e0600')})
    checks.append('0xFE7440 has one caller, the post driver at 0xF85593')
    # The z-pass's previous view-projections are projections alone: the packer sends view-info +0x220 and +0x260 as
    # CV_PrevViewProjMatr (+0x110) and CV_PrevViewProjNearestMatr (+0x150); both builders store the world projection
    # at +0x220 and at +0x260 the near projection 0xFB1530 makes, which +0xA0 (the near VP) is built from.
    witness('the per-view packer 0xFB57A0',{'+0x220':(0xfb5d13,'f30f108620020000'),'to +0x110':(0xfb5d9b,'f30f118710010000'),
                                            '+0x260':(0xfb5e23,'f30f108660020000'),'to +0x150':(0xfb5eab,'f30f118750010000')})
    assert image.call_target(0xfb2d75)==NEAR_PROJECTION and image.call_target(0xfb437b)==NEAR_PROJECTION
    witness('view-info builders',{'0xFB2AC0: the near projection to +0x260':(0xfb3da7,'f30f118360020000'),
                                  '0xFB4280: the near projection to +0x260':(0xfb4eda,'f30f119660020000')})
    checks.append("first-person motion: the near projection alone (+0x260), so NearViewStereo moves it with each eye")

    # Both eyes draw one render view, and its objects' motion constants are built once. SwitchUsageMode returns for an
    # unchanged mode and only its Reading branch compiles, so with ReadingDone held back after the first submission
    # (DualSceneStereo) the second submission's switch to Reading does nothing.
    witness('SwitchUsageMode 0xEE82F0',{'an unchanged mode returns':(0xee8306,'3b51100f84cd020000')})
    assert branches_to(image,COMPILE_OBJECTS)==[0xee859c]
    assert sorted(branches_to(image,COMPILE))==[0xee4808,0xee486b,0xee4960,0xf8ff74]
    assert branches_to(image,INSTANCE_CONSTANTS)==[0xeffe7d]
    checks.append("each object's constants: only from SwitchUsageMode's Reading branch, or a pass's per-draw compile")
    # The two cases: the passes' per-draw compile (0xF8FD40) runs below r_GraphicsPipeline 2, whose registered default
    # is 2 and which RT_BeginFrame latches at renderer +0x805C. From 2 they draw compiled objects, which bind the
    # constants at +0xD8; neither that draw nor anything but the two callers above calls Compile.
    assert image.rip_target(0xf8fd01,7,2)==RENDERER+0x805c and at(0xf8fd07,1)==b'\x02' and at(0xf8fd0c,2)==b'\x7c\x0e'
    assert image.call_target(0xf8fd15)==0xf37f50 and image.call_target(0xf8fd1f)==0xf8fd40
    assert image.cstring(0x1c823f0)=='r_GraphicsPipeline' and image.rip_target(0xec7367,7,3)==0x1c823f0
    assert image.rip_target(0xec735b,7,3)==GRAPHICS_PIPELINE and image.rip_target(0xf7d86d,6,2)==GRAPHICS_PIPELINE
    witness('r_GraphicsPipeline',{'edi cleared in the registration':(0xec72eb,'33ff'),'its default, edi + 2':(0xec7352,'448d4f02'),
                                  'latched at renderer +0x805C':(0xf7d876,'89835c800000')})
    witness('compiled draw 0xF00A90',{'its per-instance constants':(0xf00b13,'4c8b87d8000000')})
    # The world's previous projection: the second eye's way out of that. And why objects at rest have motion at all: a
    # character is flagged while its movement passes a threshold of 0 (0x81BC90).
    witness('view-info builder 0xFB2AC0',{'the world projection to +0x220':(0xfb3d2f,'f3440f118320020000')})
    assert image.cstring(0x1d2efd0)=='ca_MotionBlurMovementThreshold' and image.rip_target(0x8e8422,7,3)==0x1d2efd0
    witness('ca_MotionBlurMovementThreshold',{'default 0':(0x8e841a,'0f57db'),
                                              'the check: threshold against +0xAD4':(0x81bc98,'0f2f81d40a0000')})
    checks.append("world motion: the second eye's constants hold the first eye's previous view, so +0x220 must move")

    # TemporalAaNative.h: the bodies the probe's gate checks, and the constants it reads through.
    entries=native_rows(HEADER)
    assert [(rva,name) for rva,_,_,name in entries]==[(POST_DRIVER,'PostDriver'),(RESOLVE,'TemporalResolve'),
        (0xfe3760,'AaModeFlags'),(0xecec10,'AaModeClamp'),(0xfb2a00,'PipelineAaExecute'),(VIEW_INFO,'ViewInfo'),
        (SET_PREVIOUS_CAMERA,'SetPreviousFrameCamera'),(PREV_VIEW_STORE,'PrevViewStore'),(PREV_VIEW_SLOT,'PrevViewSlot'),
        (SET_VIEWPORT,'SetViewport'),(COMPILE_OBJECTS,'CompileObjects'),(INSTANCE_CONSTANTS,'InstanceConstants'),
        (PREV_WORLD,'PrevWorld'),(MOTION_MATRIX,'PrevInViewSpace'),(DRAW_COMPILED,'DrawCompiled')],entries
    relocs=image.relocations()
    for rva,size,expected,name in entries:
        b=at(rva,size)
        assert fnv(b)==expected,name
        assert not any(rva<=p<rva+size for p in relocs),name+' relocates in memory'
        corrupt=bytearray(b);corrupt[len(b)//2]^=1
        assert fnv(corrupt)!=expected,name+' mutation control'
        assert at(rva+size,1)==b'\xcc',name+' is the whole function'
        checks.append(name+': complete body (padding after it), no base relocations, mutation refused')
    mine={rva for rva,_,_,_ in entries}
    for header in sorted((ROOT/'src').rglob('*Native.h')):
        if header.name!=HEADER.name:assert not mine&{rva for rva,_,_,_ in native_rows(header)},header.name
    checks.append('no row in another *Native.h table')
    for name,value in {'FrameCounter':COUNTER,'ModeCvar':MODE_CVAR,'ModeEffective':MODE_EFFECTIVE,
                       'History':HISTORY[0],'HistorySlots':len(HISTORY),'TextureDevice':0x30,'Pipeline':PIPELINE,
                       'PipelineAaStage':0x288,'PipelineView':0x2e0,'StageLastResolve':0x20,
                       'RendererViewProj':0xcf0,'RendererPrevViewProj':0xd30,'ViewProjSize':0x40,
                       'RendererJitter':0xd70,'SetPreviousFrameCamera':SET_PREVIOUS_CAMERA,
                       'ViewPreviousCamera':0x13e0,'CameraSize':0x240,'RendererPrevViews':0xab0,
                       'RendererViewport':0x8060,'PrevViewSlots':8,'PrevViewSize':0x40,
                       'RendererGraphicsPipeline':0x805c,'CompiledOncePipeline':2}.items():
        assert header_constant(name)==value,name
    assert HISTORY[1]==HISTORY[0]+8
    checks.append("TemporalAaNative.h's constants are the ones witnessed above")
    return checks

if __name__=='__main__':
    try:checks=verify()
    except AssertionError as e:
        print('FAIL',e);sys.exit(1)
    for c in checks:print('ok  ',c)
    print(f'verify_temporal_aa: {len(checks)} checks passed')
