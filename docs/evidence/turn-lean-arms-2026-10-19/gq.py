"""Batch Ghidra queries over /Prey/PreyDll.dll (read-only).

Usage: python gq.py OUTFILE CMD [CMD ...]
Each CMD is one string: "d 0x1234" decompile containing function,
"l 0x1234 40" listing N instructions from rva, "lf 0x1234" listing of whole function,
"x 0x1234" xrefs to, "v 0x1D30BD0 0x100 0x360" vtable slots from off a to b,
"s text" find string + xrefs, "c 0x1234" calls made by function.
"""
import sys, os
os.environ.setdefault("GHIDRA_INSTALL_DIR", r"C:\Users\SPEC06\Tools\ghidra_12.1.4_PUBLIC")
import pyghidra
pyghidra.start()
from ghidra.base.project import GhidraProject
from ghidra.app.decompiler import DecompInterface, DecompileOptions
from ghidra.util.task import ConsoleTaskMonitor

proj = GhidraProject.openProject(os.path.join(os.path.dirname(os.path.abspath(__file__)), "gproj"), "PreyVR", True)
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

out = open(sys.argv[1], "w", encoding="utf-8")


def P(*a):
    out.write(" ".join(str(x) for x in a) + "\n")


def A(rva):
    return base.add(rva)


def R(addr):
    return addr.getOffset() - B


def fname(addr):
    f = fm.getFunctionContaining(addr)
    return ("%s@0x%X" % (f.getName(), R(f.getEntryPoint()))) if f else "?"


def ins_line(ins):
    s = "%08X  %-40s" % (R(ins.getAddress()), str(ins))
    refs = []
    for r in ins.getReferencesFrom():
        to = r.getToAddress()
        if to.isMemoryAddress():
            t = R(to)
            note = "0x%X" % t
            f = fm.getFunctionAt(to)
            if f:
                note += "(fn)"
            d = listing.getDataAt(to)
            if d is not None and d.hasStringValue():
                note += ' "%s"' % str(d.getValue())[:80]
            refs.append(note)
    if refs:
        s += " ; " + ", ".join(refs)
    return s


for cmd in sys.argv[2:]:
    parts = cmd.split()
    op = parts[0]
    P("\n######## " + cmd)
    try:
        if op == "d":
            addr = A(int(parts[1], 16))
            f = fm.getFunctionContaining(addr)
            if f is None:
                P("// no function at", parts[1])
                continue
            r = di.decompileFunction(f, 300, mon)
            P("//==== %s entry=0x%X size=%d" % (f.getName(), R(f.getEntryPoint()), f.getBody().getNumAddresses()))
            P(r.getDecompiledFunction().getC() if r.decompileCompleted() else "// failed " + str(r.getErrorMessage()))
        elif op == "l":
            addr = A(int(parts[1], 16))
            n = int(parts[2]) if len(parts) > 2 else 40
            ins = listing.getInstructionAt(addr) or listing.getInstructionAfter(addr)
            for _ in range(n):
                if ins is None:
                    break
                P(ins_line(ins))
                ins = ins.getNext()
        elif op == "lf":
            addr = A(int(parts[1], 16))
            f = fm.getFunctionContaining(addr)
            P("// %s" % fname(addr))
            for ins in listing.getInstructions(f.getBody(), True):
                P(ins_line(ins))
        elif op == "x":
            addr = A(int(parts[1], 16))
            for ref in rm.getReferencesTo(addr):
                fr = ref.getFromAddress()
                P("  0x%X %s in %s" % (R(fr), ref.getReferenceType(), fname(fr)))
        elif op == "v":
            vt = int(parts[1], 16)
            a0 = int(parts[2], 16) if len(parts) > 2 else 0
            a1 = int(parts[3], 16) if len(parts) > 3 else 0x40
            for off in range(a0, a1, 8):
                q = mem.getLong(A(vt + off))
                t = q - B
                f = fm.getFunctionAt(base.add(t)) if 0 < t < 0x10000000 else None
                P("slot +0x%X -> 0x%X %s size=%s" % (off, t, f.getName() if f else "-", f.getBody().getNumAddresses() if f else "-"))
        elif op == "s":
            text = cmd[2:]
            n = 0
            global STRS
            try:
                STRS
            except NameError:
                STRS = [d for d in listing.getDefinedData(True) if d.hasStringValue()]
            for d in STRS:
                v = d.getValue()
                if v is not None and text in str(v):
                    n += 1
                    P('str 0x%X "%s"' % (R(d.getAddress()), str(v)[:120]))
                    for ref in rm.getReferencesTo(d.getAddress()):
                        fr = ref.getFromAddress()
                        P("    ref 0x%X in %s" % (R(fr), fname(fr)))
                    if n > 40:
                        break
        elif op == "c":
            addr = A(int(parts[1], 16))
            f = fm.getFunctionContaining(addr)
            for ins in listing.getInstructions(f.getBody(), True):
                if ins.getFlowType().isCall() or (ins.getFlowType().isJump() and ins.getFlowType().isUnConditional()):
                    P(ins_line(ins))
        elif op == "q":
            vt = int(parts[1], 16)
            n = int(parts[2]) if len(parts) > 2 else 8
            for k in range(n):
                q = mem.getLong(A(vt + k * 8))
                P("0x%X: 0x%X (rva 0x%X)" % (vt + k * 8, q, q - B))
        else:
            P("?? unknown op")
    except Exception as e:
        import traceback
        P("ERROR", e, traceback.format_exc())
    out.flush()

out.close()
proj.close()
