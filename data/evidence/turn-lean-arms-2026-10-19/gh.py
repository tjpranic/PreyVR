"""Read-only pyghidra helper over /Prey/PreyDll.dll.

Usage: python gh.py OUTFILE "cmd args" "cmd args" ...
Commands (addresses are RVAs, hex):
  d RVA...            decompile the function containing each RVA
  af RVA              listing of the whole function containing RVA
  a RVA N             N instructions from RVA
  v VT N              vtable slots
  x RVA...            references to RVA
  s TEXT              defined strings containing TEXT (+ xrefs)
  calls RVA           direct/indirect calls inside the function containing RVA
  q RVA N             N qwords/floats at RVA
"""
import sys
import pyghidra
pyghidra.start()
from ghidra.base.project import GhidraProject
from ghidra.app.decompiler import DecompInterface, DecompileOptions
from ghidra.util.task import ConsoleTaskMonitor

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
mem = prog.getMemory()
di = DecompInterface()
di.openProgram(prog)
mon = ConsoleTaskMonitor()

def A(r):
    return base.add(r)

def rva(addr):
    return addr.getOffset() - B

def fname(addr):
    f = fm.getFunctionContaining(addr)
    return ("%s@0x%X" % (f.getName(), rva(f.getEntryPoint()))) if f else "?"

for cmd in sys.argv[2:]:
    parts = cmd.split()
    m, args = parts[0], parts[1:]
    P("\n######## " + cmd)
    try:
        if m == "d":
            for a in args:
                f = fm.getFunctionContaining(A(int(a, 16)))
                if f is None:
                    P("// no function at", a)
                    continue
                r = di.decompileFunction(f, 300, mon)
                P("//==== %s %s entry=0x%X" % (a, f.getName(), rva(f.getEntryPoint())))
                P(r.getDecompiledFunction().getC() if r.decompileCompleted() else "// failed " + str(r.getErrorMessage()))
        elif m == "af":
            f = fm.getFunctionContaining(A(int(args[0], 16)))
            if f is None:
                P("no function")
                continue
            for ins in listing.getInstructions(f.getBody(), True):
                note = ""
                for ref in ins.getReferencesFrom():
                    t = ref.getToAddress()
                    if t.isMemoryAddress():
                        d = listing.getDataAt(t)
                        note += " ->0x%X" % rva(t)
                        if d is not None and d.hasStringValue():
                            note += ' "%s"' % str(d.getValue())[:60]
                        elif ins.getFlowType().isCall():
                            note += " " + fname(t)
                P("%08X  %s%s" % (rva(ins.getAddress()), ins, note))
        elif m == "a":
            ins = listing.getInstructionAt(A(int(args[0], 16)))
            if ins is None:
                ins = listing.getInstructionAfter(A(int(args[0], 16)))
            for _ in range(int(args[1])):
                if ins is None:
                    break
                note = ""
                for ref in ins.getReferencesFrom():
                    t = ref.getToAddress()
                    if t.isMemoryAddress():
                        d = listing.getDataAt(t)
                        note += " ->0x%X" % rva(t)
                        if d is not None and d.hasStringValue():
                            note += ' "%s"' % str(d.getValue())[:60]
                        elif ins.getFlowType().isCall():
                            note += " " + fname(t)
                P("%08X  %s%s" % (rva(ins.getAddress()), ins, note))
                ins = ins.getNext()
        elif m == "v":
            vt = int(args[0], 16)
            for i in range(int(args[1])):
                ptr = mem.getLong(A(vt + i * 8)) - B
                f = fm.getFunctionAt(A(ptr))
                size = f.getBody().getNumAddresses() if f else -1
                P("slot +0x%X -> 0x%X size=%d %s" % (i * 8, ptr, size, f.getName() if f else ""))
        elif m == "x":
            for a in args:
                P("== xrefs to", a)
                for ref in rm.getReferencesTo(A(int(a, 16))):
                    fr = ref.getFromAddress()
                    P("  0x%X %s in %s" % (rva(fr), ref.getReferenceType(), fname(fr)))
        elif m == "s":
            text = " ".join(args)
            for d in listing.getDefinedData(True):
                if d.hasStringValue():
                    v = str(d.getValue())
                    if text in v:
                        P('0x%X "%s"' % (rva(d.getAddress()), v[:120]))
                        for ref in rm.getReferencesTo(d.getAddress()):
                            P("    from 0x%X in %s" % (rva(ref.getFromAddress()), fname(ref.getFromAddress())))
        elif m == "calls":
            f = fm.getFunctionContaining(A(int(args[0], 16)))
            for ins in listing.getInstructions(f.getBody(), True):
                if ins.getFlowType().isCall():
                    tgt = [rva(r.getToAddress()) for r in ins.getReferencesFrom() if r.getToAddress().isMemoryAddress()]
                    P("%08X  %s  %s" % (rva(ins.getAddress()), ins, " ".join("0x%X %s" % (t, fname(A(t))) for t in tgt)))
        elif m == "q":
            r0 = int(args[0], 16)
            import struct
            for i in range(int(args[1])):
                v = mem.getLong(A(r0 + i * 8)) & 0xFFFFFFFFFFFFFFFF
                lo = v & 0xFFFFFFFF
                hi = v >> 32
                f1 = struct.unpack("<f", struct.pack("<I", lo))[0]
                f2 = struct.unpack("<f", struct.pack("<I", hi))[0]
                P("0x%X: %016X  f=%g %g  d=%g" % (r0 + i * 8, v, f1, f2, struct.unpack("<d", struct.pack("<Q", v))[0]))
        else:
            P("unknown", m)
    except Exception as e:
        import traceback
        P("ERROR", e, traceback.format_exc())
    out.flush()

proj.close()
out.close()
