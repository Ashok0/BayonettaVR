"""Disassemble an x86 PE range given as an absolute virtual address."""

import sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32


image_path = sys.argv[1]
start_va = int(sys.argv[2], 0)
size = int(sys.argv[3], 0) if len(sys.argv) > 3 else 512
pe = pefile.PE(image_path, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
rva = start_va - base
data = pe.get_memory_mapped_image()[rva:rva + size]
decoder = Cs(CS_ARCH_X86, CS_MODE_32)
for instruction in decoder.disasm(data, start_va):
    print(f"{instruction.address:08X}  {instruction.mnemonic:<8} {instruction.op_str}")
