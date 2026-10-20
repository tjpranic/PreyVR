import sys, pyghidra
pyghidra.start()
from ghidra.base.project import GhidraProject
proj = GhidraProject.openProject(r"C:\Users\SPEC06\Tools\ghidra-projects", "PreyVR", True)
prog = proj.openProgram("/Prey", "PreyDll.dll", True)
base = prog.getImageBase(); listing = prog.getListing(); fm = prog.getFunctionManager()
needles = [s.lower() for s in sys.argv[3:]]
lo = base.add(int(sys.argv[1],16)); hi = base.add(int(sys.argv[2],16))
it = listing.getInstructions(lo, True)
for ins in it:
    if ins.getAddress().compareTo(hi) > 0: break
    t = str(ins).lower()
    for n in needles:
        if n in t:
            f = fm.getFunctionContaining(ins.getAddress())
            print("0x%X  fn=%s  %s" % (ins.getAddress().subtract(base), ("0x%X" % f.getEntryPoint().subtract(base)) if f else "?", ins))
proj.close()
