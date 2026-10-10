"""List direct absolute-memory references to an address range in an x86 PE."""

import argparse

import pefile
from capstone import CS_ARCH_X86, CS_MODE_32, Cs
from capstone.x86 import X86_OP_MEM


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("image")
    parser.add_argument("address", type=lambda value: int(value, 0))
    parser.add_argument("size", nargs="?", type=lambda value: int(value, 0), default=4)
    args = parser.parse_args()

    pe = pefile.PE(args.image, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    disassembler = Cs(CS_ARCH_X86, CS_MODE_32)
    disassembler.detail = True
    end = args.address + args.size
    for section in pe.sections:
        if not section.Characteristics & 0x20000000:
            continue
        start = base + section.VirtualAddress
        for instruction in disassembler.disasm(section.get_data(), start):
            for operand in instruction.operands:
                if operand.type != X86_OP_MEM:
                    continue
                memory = operand.mem
                address = memory.disp & 0xFFFFFFFF
                if memory.base == 0 and memory.index == 0 and args.address <= address < end:
                    print(f"{instruction.address:08X}  {instruction.mnemonic:<8} "
                          f"{instruction.op_str:<44} absolute=0x{address:08X}")


if __name__ == "__main__":
    main()
