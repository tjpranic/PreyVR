import re, sys
data = open(r"E:\Steam Games\steamapps\common\Prey\Binaries\Danielle\x64\Release\PreyDll.dll","rb").read()
pat = re.compile(rb"[\x20-\x7e]{4,}")
words = [w.lower().encode() for w in sys.argv[1:]]
for m in pat.finditer(data):
    s = m.group().lower()
    if any(w in s for w in words):
        print(hex(m.start()), m.group().decode())
