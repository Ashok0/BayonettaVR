"""List absolute-memory writes in an x86 PE virtual-address range."""

import argparse

import pefile
from capstone import CS_AC_WRITE, CS_ARCH_X86, CS_MODE_32, Cs
from capstone.x86 import X86_OP_MEM


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("image")
    parser.add_argument("address", type=lambda value: int(value, 0))
    parser.add_argument("size", type=lambda value: int(value, 0))
    args = parser.parse_args()

    pe = pefile.PE(args.image)
    base = pe.OPTIONAL_HEADER.ImageBase
    rva = args.address - base
    code = pe.get_memory_mapped_image()[rva:rva + args.size]
    disassembler = Cs(CS_ARCH_X86, CS_MODE_32)
    disassembler.detail = True
    for instruction in disassembler.disasm(code, args.address):
        for operand in instruction.operands:
            if operand.type != X86_OP_MEM or not operand.access & CS_AC_WRITE:
                continue
            memory = operand.mem
            if memory.base == 0 and memory.index == 0:
                print(f"{instruction.address:08X}  {instruction.mnemonic:<8} "
                      f"{instruction.op_str:<42} absolute=0x{memory.disp & 0xffffffff:08X}")


if __name__ == "__main__":
    main()
