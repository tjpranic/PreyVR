from p0_friction import strings, leas, function_of
from re_disasm import window
for rva,s in strings(rb"[Bb]ouncyness|SurfaceType|surfacetypes|SurfaceTypes\.xml"):
    refs=leas.get(rva,[])
    print(hex(rva), len(refs), repr(s[:80]), [hex(function_of(r)[0]) for r in refs[:6] if function_of(r)])
r = leas[0x1c9da18][0]
print(window(r, before=0x60, after=0x160))
