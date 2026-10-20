import sys, pyghidra
pyghidra.start()
from ghidra.base.project import GhidraProject
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
proj = GhidraProject.openProject(r"C:\Users\SPEC06\Tools\ghidra-projects", "PreyVR", True)
prog = proj.openProgram("/Prey", "PreyDll.dll", True)
base = prog.getImageBase()
fm = prog.getFunctionManager()
di = DecompInterface(); di.openProgram(prog)
mode = sys.argv[1]
args = sys.argv[2:]
if mode == "d":
    for a in args:
        addr = base.add(int(a, 16))
        f = fm.getFunctionContaining(addr)
        if f is None:
            print("// no function at", a); continue
        r = di.decompileFunction(f, 120, ConsoleTaskMonitor())
        print("//==== %s  %s entry=0x%X" % (a, f.getName(), f.getEntryPoint().subtract(base)))
        print(r.getDecompiledFunction().getC() if r.decompileCompleted() else "// failed " + r.getErrorMessage())
elif mode == "x":
    rm = prog.getReferenceManager()
    for a in args:
        addr = base.add(int(a, 16))
        print("== xrefs to", a)
        for ref in rm.getReferencesTo(addr):
            fr = ref.getFromAddress()
            f = fm.getFunctionContaining(fr)
            print("  0x%X %s in %s" % (fr.subtract(base), ref.getReferenceType(), (f.getName() + " 0x%X" % f.getEntryPoint().subtract(base)) if f else "?"))
elif mode == "v":
    mem = prog.getMemory()
    vt = int(args[0],16); n = int(args[1])
    for i in range(n):
        a = base.add(vt + i*8)
        ptr = mem.getLong(a) - base.getOffset()
        f = fm.getFunctionAt(base.add(ptr))
        body = f.getBody().getNumAddresses() if f else -1
        line = "slot +0x%X -> 0x%X size=%d" % (i*8, ptr, body)
        if f and body < 120:
            r = di.decompileFunction(f, 60, ConsoleTaskMonitor())
            if r.decompileCompleted():
                c = r.getDecompiledFunction().getC()
                c = " ".join(x.strip() for x in c.splitlines() if x.strip())
                line += "  | " + c[:400]
        print(line)
elif mode == "s":
    # refs to global G; follow next instrs for [reg+0x1a8] then [reg2+off] with off in wanted
    listing = prog.getListing(); rm = prog.getReferenceManager()
    g = base.add(int(args[0],16)); wanted = [int(x,16) for x in args[1].split(",")]
    hits = {}
    for ref in rm.getReferencesTo(g):
        ins = listing.getInstructionAt(ref.getFromAddress())
        if ins is None: continue
        trail = []
        cur = ins; seen1a8 = False
        for k in range(14):
            cur = cur.getNext()
            if cur is None: break
            t = str(cur)
            trail.append(t)
            if "0x1a8]" in t: seen1a8 = True
            elif seen1a8:
                for w in wanted:
                    if ("+ 0x%x]" % w) in t:
                        f = fm.getFunctionContaining(cur.getAddress())
                        key = "0x%X" % cur.getAddress().subtract(base)
                        hits[key] = "%s fn=%s  %s" % (key, ("0x%X" % f.getEntryPoint().subtract(base)) if f else "?", t)
            if cur.getFlowType().isCall() or cur.getFlowType().isTerminal(): 
                if seen1a8 and k>10: break
    for k in sorted(hits): print(hits[k])
elif mode == "m":
    mem = prog.getMemory()
    for a in args:
        addr = base.add(int(a,16))
        blk = mem.getBlock(addr)
        cu = prog.getListing().getCodeUnitContaining(addr)
        print("0x%s block=%s cu=%s" % (a, blk.getName() if blk else None, cu))
        sym = prog.getSymbolTable().getPrimarySymbol(addr)
        print("   sym", sym)
        # dump 6 qwords around
        for k in range(-4,5):
            q = base.add(int(a,16)+k*8)
            try:
                v = mem.getLong(q)
                print("   +%d 0x%X : 0x%X" % (k*8, int(a,16)+k*8, v - base.getOffset() if v > base.getOffset() else v))
            except Exception as e:
                print("   err", e)
proj.close()
