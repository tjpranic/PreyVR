import sys, pyghidra
pyghidra.start()
from ghidra.base.project import GhidraProject
proj = GhidraProject.openProject(r"C:\Users\SPEC06\Tools\ghidra-projects", "PreyVR", True)
prog = proj.openProgram("/Prey", "PreyDll.dll", True)
base = prog.getImageBase(); mem = prog.getMemory()
for vt in sys.argv[2:]:
    for off in sys.argv[1].split(","):
        o=int(off,16)
        print("vt 0x%s +0x%X -> 0x%X" % (vt, o, mem.getLong(base.add(int(vt,16)+o)) - base.getOffset()))
proj.close()
