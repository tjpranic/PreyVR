# Replica of handpose:: maths + AnatomicalWrist/GripPointLocal, fed with the rig bones
# from run player-20261025-161415 (ik.skel) and gripInAim from headset logs.
import numpy as np, math
def qmul(a,b):
    ax,ay,az,aw=a;bx,by,bz,bw=b
    return np.array([aw*bx+ax*bw+ay*bz-az*by, aw*by-ax*bz+ay*bw+az*bx, aw*bz+ax*by-ay*bx+az*bw, aw*bw-ax*bx-ay*by-az*bz])
def qnorm(q): return np.array(q,float)/np.linalg.norm(q)
def qconj(q): return np.array([-q[0],-q[1],-q[2],q[3]])
def rot(q,v):
    u=np.array(q[:3]);s=q[3];v=np.array(v,float)
    return 2*np.dot(u,v)*u+(s*s-np.dot(u,u))*v+2*s*np.cross(u,v)
def unit(v): return np.array(v,float)/np.linalg.norm(v)
def about_z(r): return np.array([0,0,math.sin(r/2),math.cos(r/2)])
def from_basis(c0,c1,c2):
    m=np.column_stack([c0,c1,c2]);t=np.trace(m)
    if t>0:
        s=math.sqrt(t+1)*2;return np.array([(m[2,1]-m[1,2])/s,(m[0,2]-m[2,0])/s,(m[1,0]-m[0,1])/s,s/4])
    i=int(np.argmax(np.diag(m)));j=(i+1)%3;k=(j+1)%3
    s=math.sqrt(1+m[i,i]-m[j,j]-m[k,k])*2;q=[0,0,0,0];q[i]=s/4;q[j]=(m[j,i]+m[i,j])/s;q[k]=(m[k,i]+m[i,k])/s;q[3]=(m[k,j]-m[j,k])/s
    return np.array(q)
# kLeft / kFist (HandPose.cpp)
BASE={'index':(0.0522,-0.0170,0.0482,0.9973),'middle':(-0.0149,0.0216,0.0393,0.9989),'ring':(-0.0716,0.0452,-0.0018,0.9964),'pinky':(-0.1240,0.0845,0.0121,0.9886)}
OPEN=(-8,-14,-8); SHUT=(-72,-92,-50)
def shape(side,q,flex):
    r=qnorm(qmul(np.array(q,float),about_z(math.radians(flex))))
    return np.array([-r[0],-r[1],r[2],r[3]]) if side=='R' else r
def relq(side,finger):  # base rotation at curl=1
    return shape(side,BASE[finger],0)
# bone offsets (ik.skel rel translations)
B={'R':{'index':((0.0351,0.0024,-0.0259),(0.0579,0,0)),'middle':((0.0343,0.0054,-0.0024),(0.0596,0,0)),
        'ring':((0.0315,0.0059,0.0215),(0.0543,0,0)),'pinky':((0.0281,-0.0034,0.0359),(0.0500,0,0))},
   'L':{'index':((0.0351,-0.0015,0.0259),(0.0578,-0.0034,0.0002)),'middle':((0.0345,0.0032,0.0027),(0.0595,-0.0032,0.0001)),
        'ring':((0.0318,0.0056,-0.0211),(0.0542,-0.0027,-0.0006)),'pinky':((0.0279,-0.0024,-0.0361),(0.0499,-0.0022,-0.0006))}}
IDX={'R':[(0.0579,0,0),(0.0390,0,0),(0.0276,0,0)],'L':[(0.0578,-0.0034,0.0002),(0.0389,-0.0023,0.0002),(0.0275,-0.0016,-0.0001)]}
def frame(side):
    kn=[]
    for f in ('index','middle','ring','pinky'):
        o,first=B[side][f];kn.append(np.array(o)+rot(relq(side,f),first))
    mean=sum(kn)/4;d=np.linalg.norm(mean);L=unit(mean)
    a=kn[0]-kn[3];a=unit(a-L*np.dot(a,L))
    p=unit(np.cross(L,a) if side=='L' else np.cross(a,L))
    return L,a,p,d
def grip_frame(side,lean):
    c,s=math.cos(lean),math.sin(lean)
    return np.array([0,-c,-s]),np.array([0,s,-c]),np.array([1.,0,0]) if side=='L' else np.array([-1.,0,0])
def f2f(fr,to):
    e=np.eye(3);cols=[]
    for k in range(3):
        a,b,p=np.dot(fr[0],e[k]),np.dot(fr[1],e[k]),np.dot(fr[2],e[k])
        cols.append(to[0]*a+to[1]*b+to[2]*p)
    return from_basis(*cols)
def fk(side,curl,fist):
    fl=[o*curl*(1-fist)+s*fist for o,s in zip(OPEN,SHUT)]
    q=relq(side,'index');t=np.array(B[side]['index'][0]);pts=[t.copy()];qs=[q]
    for off,f in zip(IDX[side],fl):
        t=t+rot(q,off);q=qmul(q,shape(side,(0,0,0,1),f));pts.append(t.copy());qs.append(q)
    tip=t+rot(q,(0.022,0,0));pts.append(tip);return pts,qs
def ang(a,b): return math.degrees(math.acos(np.clip(np.dot(unit(a),unit(b)),-1,1)))
for side,off in (('R',(1.6,-3.5,7.4)),('L',(-1.6,-3.5,7.4))):
    L,T,P,d=frame(side)
    print(f"== {side} control: knuckle={d:.4f} len={np.round(L,3)} palm={np.round(P,3)} (log: 0.0881; lenL .993,.009,-.121; palmL .017,-.998,.066)")
    lean=math.radians(15);g=grip_frame(side,lean);hig=f2f((L,T,P),g)
    gp=L*.55*d+P*.035
    print(f"   wristToGrip={np.linalg.norm(gp)*100:.1f}cm (log 6.0)  lenInGrip={np.round(rot(hig,L),2)} (log 0,-.97,-.26)")
    rel=qnorm((0.365,0,0,0.931))
    aimdir=rot(qconj(rel),(0,0,-1)); aimpos=rot(qconj(rel),-np.array(off))  # cm
    print(f"   aim origin in grip={np.round(aimpos,1)}cm  aim dir in grip={np.round(aimdir,3)}  hand len vs aim={ang(rot(hig,L),aimdir):.1f}deg")
    for name,c,f in (('pointing (off trigger)',.25,0),('relaxed/no follow',1,0),('resting (touch)',1,.2),('half trigger',1,.6),('full trigger',1,1)):
        pts,qs=fk(side,c,f)
        tipg=rot(hig,pts[-1]-gp)*100; k=rot(hig,pts[1]-gp)*100
        chord=rot(hig,pts[-1]-pts[1]); prox=rot(hig,rot(qs[1],(1,0,0)))
        dist=np.linalg.norm(tipg-aimpos); along=np.dot(tipg-aimpos,aimdir); perp=np.linalg.norm((tipg-aimpos)-aimdir*along)
        print(f"   {name:24s} tip in grip={np.round(tipg,1)}cm | tip->aim origin {dist:.1f}cm (along ray {along:+.1f}, off ray {perp:.1f}) | chord vs aim {ang(chord,aimdir):.1f}deg, prox vs aim {ang(prox,aimdir):.1f}deg")
    p0,_=fk(side,.25,0);r0,_=fk(side,1,.2);f0,_=fk(side,1,1)
    print(f"   tip travel pointing->resting {np.linalg.norm(p0[-1]-r0[-1])*100:.1f}cm, pointing->full {np.linalg.norm(p0[-1]-f0[-1])*100:.1f}cm")
    for lean_deg in (5,15,25):
        g2=grip_frame(side,math.radians(lean_deg));h2=f2f((L,T,P),g2)
        p,_=fk(side,.25,0);print(f"   lean {lean_deg:2d}deg: chord vs aim {ang(rot(h2,p[-1]-p[1]),aimdir):.1f}deg")
print("\n=== decomposition ===")
for side,off in (('R',(1.6,-3.5,7.4)),('L',(-1.6,-3.5,7.4))):
    L,T,P,d=frame(side);hig=f2f((L,T,P),grip_frame(side,math.radians(15)));gp=L*.55*d+P*.035
    rel=qnorm((0.365,0,0,0.931));aimdir=rot(qconj(rel),(0,0,-1));aimpos=rot(qconj(rel),-np.array(off))
    pts,qs=fk(side,.25,0)
    g=[rot(hig,p-gp)*100 for p in pts]   # cm, grip frame: base, knuckle(index1), pip, dip, ... tip
    chord=unit(g[-1]-g[1])
    c_a=rot(rel,chord)                   # chord in aim frame: -Z forward, +Y up, +X right
    pitch=math.degrees(math.atan2(c_a[1],-c_a[2])); yaw=math.degrees(math.atan2(c_a[0],-c_a[2]))
    print(f"{side}: knuckle in grip {np.round(g[1],1)}  tip {np.round(g[-1],1)}  finger len knuckle->tip {np.linalg.norm(g[-1]-g[1]):.1f}cm")
    print(f"   aim origin rel. knuckle: {np.linalg.norm(aimpos-g[1]):.1f}cm; in aim-frame cm (x right,y up,z back) = {np.round(rot(rel,aimpos-g[1]),1)}")
    print(f"   tip rel. aim origin, aim frame cm = {np.round(rot(rel,g[-1]-aimpos),1)}")
    print(f"   chord vs aim: pitch {pitch:+.1f}deg (neg=below ray), yaw {yaw:+.1f}deg (+=right)")
    old_start=aimpos-aimdir*10.0   # today's selector ray start: 100 mm behind aim origin
    print(f"   selector start today (grip cm) {np.round(old_start,1)}, dist from grip origin {np.linalg.norm(old_start):.1f}cm")
    back=np.dot(g[-1]-old_start,chord); perp=np.linalg.norm((g[-1]-old_start)-chord*back)
    print(f"   back-offset from tip to reach same spot: {back:.1f}cm along finger (passes {perp:.1f}cm from it); to reach grip origin: {np.dot(g[-1],chord):.1f}cm")
    print(f"   finger axis in grip frame (unit) {np.round(chord,3)}  -> pointerInGrip constant candidate")
