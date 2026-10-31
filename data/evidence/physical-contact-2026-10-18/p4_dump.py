import sys
sys.path.insert(0, r"C:\Users\SPEC06\Documents\GitHub\PreyVR\data\evidence\physical-contact-2026-10-18")
from re_disasm import dis_function
print(dis_function(int(sys.argv[1], 16)))
