"""Find game code that asks a physical entity for pe_status_check_stance (type 0x15).

A caller stores 0x15 into a stack struct and soon calls GetStatus through the
entity's vtable (+0x30). Also lists callers of the stance overlap test 0xB56900.
Usage: python scan_stance.py OUTFILE
"""
import sys
import pyghidra
pyghidra.start()
from ghidra.base.project import GhidraProject

out = open(sys.argv[1], "w", encoding="utf-8")
def P(*a):
    out.write(" ".join(str(x) for x in a) + "\n")

proj = GhidraProject.openProject(r"C:\Users\SPEC06\Tools\ghidra-projects", "PreyVR", True)
prog = proj.openProgram("/Prey", "PreyDll.dll", True)
base = prog.getImageBase()
B = base.getOffset()
fm = prog.getFunctionManager()
listing = prog.getListing()
rm = prog.getReferenceManager()

def rva(addr):
    return addr.getOffset() - B

def fname(addr):
    f = fm.getFunctionContaining(addr)
    return ("%s@0x%X" % (f.getName(), rva(f.getEntryPoint()))) if f else "?"

hits = []
window = []
for ins in listing.getInstructions(True):
    text = str(ins)
    window.append(ins)
    if len(window) > 40:
        window.pop(0)
    if text.startswith("CALL qword ptr [R") and text.endswith("+ 0x30]"):
        for prev in window[:-1]:
            t = str(prev)
            if t.startswith("MOV dword ptr [") and t.endswith(",0x15"):
                hits.append((rva(prev.getAddress()), rva(ins.getAddress()), fname(ins.getAddress())))
                break
P("== stores of 0x15 shortly before a call through +0x30")
for store, call, f in hits:
    P("store 0x%X call 0x%X in %s" % (store, call, f))
P("== references to 0xB56900")
for ref in rm.getReferencesTo(base.add(0xB56900)):
    P("  0x%X %s in %s" % (rva(ref.getFromAddress()), ref.getReferenceType(), fname(ref.getFromAddress())))
proj.close()
out.close()
