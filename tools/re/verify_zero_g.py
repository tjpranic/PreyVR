"""Read-only Steam zero-G contracts (docs/ZERO-G-SCOPE-2026-10-30.md): how Prey rolls, pitches and brakes the
player in zero-G (lean actions -> ArkPlayerInput flags -> lean axis -> the zero-G rotation of ArkPlayerCamera's
m_rotation), and why the zero-G helmet ignores the headset (a bone attachment on the eye joint whose relative default
the HUD-helmet component rewrites each frame). Never attaches."""
import json,struct,sys
from pathlib import Path
from preydll import DEFAULT_MODULE,Image

def verify():
    image=Image(DEFAULT_MODULE);at=image.at;checks=[]
    def witness(name,rva,hexbytes):
        assert at(rva,len(bytes.fromhex(hexbytes)))==bytes.fromhex(hexbytes),name;checks.append(name)
    def calls(name,rva,target):
        assert image.call_target(rva)==target,name;checks.append(name)
    def points(name,rva,length,disp,target):
        assert image.rip_target(rva,length,disp)==target,name;checks.append(name)
    def f32(rva):return struct.unpack('<f',at(rva,4))[0]

    # The lean actions: names at the action table (0x2C16838) +0xA00/+0xA08, each bound to a handler.
    witness('action table init: leanleft at +0xA00, leanright at +0xA08',0x1709bfa,
        '488d15d7ff7600488d8b000a0000e8631fa7fe488d15d4ff7600488d8b080a0000')
    assert image.cstring(image.rip_target(0x1709bfa,7,3))=='leanleft' and image.cstring(image.rip_target(0x1709c0d,7,3))=='leanright'
    checks.append("the names are 'leanleft' and 'leanright'")
    witness('ArkPlayerInput binds +0xA00 to 0x1590250 and +0xA08 to 0x1590290',0x158db89,'488b83000a0000')
    witness('(the +0xA08 binding)',0x158dc02,'488b83080a0000')
    points('the +0xA00 handler is 0x1590250',0x158db7a,7,3,0x1590250)
    points('the +0xA08 handler is 0x1590290',0x158dbf3,7,3,0x1590290)
    # Handlers: press (1) sets the flag, release (2) clears it, both recompute the axis.
    witness('leanleft: press sets input +0xB5, release clears it, then 0x15910B0',0x1590250,
        '4883ec284183f9017513448889b5000000e84a0e000032c04883c428c34183f9027507c681b500000000e8310e000032c04883c428c3')
    witness('leanright: the same on input +0xB6',0x1590290,
        '4883ec284183f9017513448889b6000000e80a0e000032c04883c428c34183f9027507c681b600000000e8f10d000032c04883c428c3')
    calls('leanleft recomputes through 0x15910B0',0x1590261,0x15910b0)
    # The combiner: axis = (+0xB6 ? 1 : 0) + (+0xB5 ? -1 : 0) to +0x80 and +0x7C; both held sets +0xB1 (brake).
    witness('combiner 0x15910B0: the cinematic gate (+0x94) clears both flags; axis to +0x80/+0x7C; both -> +0xB1',0x15910b0,
        '83b99400000000740966c781b500000000000fb691b50000000f57c084d2740af30f100d68da6e00eb030f57c90fb681b6000000'
        '84c07408f30f100538da6e00f30f58c1f30f118180000000f30f11417c84d2740c84c07408c681b100000001c3c681b100000000c3cc')
    assert image.rip_target(0x15910d0,8,4)==0x1c7eb40 and f32(0x1c7eb40)==-1.0
    assert image.rip_target(0x15910e8,8,4)==0x1c7eb28 and f32(0x1c7eb28)==1.0
    checks.append('left is -1.0 (0x1C7EB40), right +1.0 (0x1C7EB28)')
    # Prey's re-read (StickRereadNative.h's 0x158E780) also re-reads both flags from the physical pad.
    witness('the stick re-read reads leanleft/leanright again and stores +0xB5/+0xB6',0x158eb13,
        '4981c0000a0000488b4f084489742420e818faffff488b0f4c8d0d72f0700044897424208881b50000004c8b05f47c6801488b557f'
        '4981c0080a0000488b4f08e8e8f9ffff488b0f8881b6000000488b0fe847250000')
    calls('then recomputes the axis',0x158eb64,0x15910b0)
    # The rotation: ArkPlayerCamera's update rotates its m_rotation (+0x150) through the movement FSM (ArkPlayer +0x58).
    calls('ArkPlayerCamera update 0x148A250 calls the FSM rotation 0x159E1D0',0x148a38f,0x159e1d0)
    calls('(its other branch too)',0x148a3cb,0x159e1d0)
    witness('FSM rotation, state 9: the zero-G state at FSM +0x310 (ArkPlayer +0x368)',0x159e232,'498d8a10030000')
    calls('state 9 rotates through 0x126AF50',0x159e24f,0x126af50)
    witness('the zero-G rotation reads the lean axis at ArkPlayer +0x960 (input +0x8E0 +0x80)',0x126aff4,'f30f10b760090000')
    witness('the stick turn rate (0x158F5B0) branches on FSM state 9',0x158f69c,'83785809')
    witness("ArkPlayer's state event names 'onEnterZeroG'",0x1584c57,'488d15d2288d00')
    assert image.cstring(image.rip_target(0x1584c57,7,3))=='onEnterZeroG' and image.cstring(image.rip_target(0x1584b03,7,3))=='onExitZeroG'
    checks.append("and 'onExitZeroG' (0x15849D0, stance +0x1838 == 9)")

    # The helmet: the HUD-helmet component's update (0x1327070, ArkPlayer +0x1468) writes the sway as the
    # 'helmet' bone attachment's relative default on character slots 0 and 5.
    witness('helmet update: zero-G state ArkPlayer +0x368 while FSM +0x58 is 9',0x1327530,'498d9e68030000')
    witness('(the state test)',0x1327543,'41837e5809')
    for rva in (0x1327cb9,0x1327d32):
        assert image.cstring(image.rip_target(rva,7,3))=='helmet',hex(rva)
    checks.append("both lookups are by the name 'helmet'")
    witness('slot 0: attachment vtable +0x58',0x1327d0e,'ff5058')
    witness('slot 5: attachment vtable +0x58',0x1327d87,'ff5058')
    BONE=0x1d212b8
    assert image.vtable_target(BONE+0x58)==0x828dc0 and image.vtable_target(BONE+0x68)==0x822c80
    checks.append('bone attachment vtable 0x1D212B8: +0x58 is 0x828DC0, +0x68 is 0x822C80')
    witness('0x828DC0 SetAttRelativeDefault: QuatT to +0xF8 (q) / +0x108 (t), whole body',0x828dc0,
        '8b028981f80000008b42048981fc0000008b42088981000100008b420c898104010000f20f104210f20f1181080100008b4218898110010000c3')
    witness('0x822C80 returns the model-relative transform at +0x130',0x822c80,'488d8130010000c3')
    return {'target':str(DEFAULT_MODULE),'sha256':image.sha256,'environment':'static','game_launched':False,'checks':checks,
        'limits':'Proves native bytes and call routes. Not exercised: which character slot draws the visible helmet, '
                 'whether a model transform written before the render survives to the draw, the helmet\'s mount, '
                 'or how a rolled view feels in a headset.'}

if __name__=='__main__':
    report=json.dumps(verify(),indent=2)+'\n'
    if len(sys.argv)>1:Path(sys.argv[1]).write_text(report,encoding='utf-8',newline='\n')
    print(report)
