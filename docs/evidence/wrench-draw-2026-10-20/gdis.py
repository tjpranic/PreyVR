import sys, pyghidra
pyghidra.start()
from ghidra.base.project import GhidraProject
proj = GhidraProject.openProject(r"C:\Users\SPEC06\Tools\ghidra-projects", "PreyVR", True)
prog = proj.openProgram("/Prey", "PreyDll.dll", True)
base = prog.getImageBase(); listing = prog.getListing()
start = int(sys.argv[1],16); n = int(sys.argv[2])
addr = base.add(start)
ins = listing.getInstructionAt(addr)
if ins is None:
    ins = listing.getInstructionAfter(addr)
for i in range(n):
    if ins is None: break
    print("0x%X  %s" % (ins.getAddress().subtract(base), ins))
    ins = ins.getNext()
proj.close()
