"""usage: gmix.py outfile  d:ADDR ... i:ADDR:COUNT ... v:VT:SLOT,SLOT ... x:ADDR"""
import sys, pyghidra
pyghidra.start()
from ghidra.base.project import GhidraProject
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
proj = GhidraProject.openProject(r"C:\Users\SPEC06\Tools\ghidra-projects", "PreyVR", True)
prog = proj.openProgram("/Prey", "PreyDll.dll", True)
base = prog.getImageBase(); fm = prog.getFunctionManager(); mem = prog.getMemory()
listing = prog.getListing(); rm = prog.getReferenceManager()
di = DecompInterface(); di.openProgram(prog)
out = open(sys.argv[1], "w", encoding="utf-8")
def p(*a):
    out.write(" ".join(str(x) for x in a) + "\n")
for spec in sys.argv[2:]:
    kind, rest = spec.split(":", 1)
    if kind == "d":
        addr = base.add(int(rest, 16))
        f = fm.getFunctionContaining(addr)
        if f is None:
            p("// no function at", rest); continue
        r = di.decompileFunction(f, 180, ConsoleTaskMonitor())
        p("//==== %s %s entry=0x%X size=%d" % (rest, f.getName(), f.getEntryPoint().subtract(base), f.getBody().getNumAddresses()))
        p(r.getDecompiledFunction().getC() if r.decompileCompleted() else "// failed " + r.getErrorMessage())
    elif kind == "i":
        a, n = rest.split(":")
        addr = base.add(int(a, 16))
        p("//---- instructions at", a)
        ins = listing.getInstructionAt(addr)
        if ins is None:
            # undefined bytes: dump raw
            b = [mem.getByte(addr.add(k)) & 0xff for k in range(int(n) * 6)]
            p("  raw", " ".join("%02X" % x for x in b)); continue
        for k in range(int(n)):
            if ins is None: break
            refs = [("0x%X" % r.getToAddress().subtract(base)) for r in ins.getReferencesFrom() if r.getToAddress().isMemoryAddress() and not r.getToAddress().isStackAddress()]
            p("  0x%X  %s  %s" % (ins.getAddress().subtract(base), ins, " ".join(refs)))
            ins = ins.getNext()
    elif kind == "v":
        vt, slots = rest.split(":")
        for s in slots.split(","):
            o = int(s, 16)
            p("vt 0x%s +0x%X -> 0x%X" % (vt, o, mem.getLong(base.add(int(vt, 16) + o)) - base.getOffset()))
    elif kind == "x":
        addr = base.add(int(rest, 16))
        p("== xrefs to", rest)
        for ref in rm.getReferencesTo(addr):
            fr = ref.getFromAddress(); f = fm.getFunctionContaining(fr)
            p("  0x%X %s in %s" % (fr.subtract(base), ref.getReferenceType(), ("0x%X" % f.getEntryPoint().subtract(base)) if f else "?"))
out.close()
proj.close()
