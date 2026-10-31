"""Scan instructions in an RVA range for operand text matching regexes.

Usage: python gscan.py OUTFILE LO HI REGEX [REGEX...]
"""
import sys, os, re
os.environ.setdefault("GHIDRA_INSTALL_DIR", r"C:\Users\SPEC06\Tools\ghidra_12.1.4_PUBLIC")
import pyghidra
pyghidra.start()
from ghidra.base.project import GhidraProject

proj = GhidraProject.openProject(os.path.join(os.path.dirname(os.path.abspath(__file__)), "gproj"), "PreyVR", True)
prog = proj.openProgram("/Prey", "PreyDll.dll", True)
base = prog.getImageBase()
B = base.getOffset()
fm = prog.getFunctionManager()
listing = prog.getListing()
out = open(sys.argv[1], "w", encoding="utf-8")
lo = int(sys.argv[2], 16)
hi = int(sys.argv[3], 16)
pats = [re.compile(p, re.I) for p in sys.argv[4:]]
from ghidra.program.model.address import AddressSet
aset = AddressSet(base.add(lo), base.add(hi))
n = 0
for ins in listing.getInstructions(aset, True):
    s = str(ins)
    for p in pats:
        if p.search(s):
            f = fm.getFunctionContaining(ins.getAddress())
            out.write("%08X  %-50s  in %s\n" % (ins.getAddress().getOffset() - B, s,
                      ("0x%X" % (f.getEntryPoint().getOffset() - B)) if f else "?"))
            n += 1
            break
out.write("# %d hits\n" % n)
out.close()
proj.close()
