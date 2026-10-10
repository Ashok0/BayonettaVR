"""Find direct x86 call/jump references to an absolute virtual address in a PE."""

import argparse
import struct

import pefile


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("image")
    parser.add_argument("target", type=lambda value: int(value, 0))
    args = parser.parse_args()

    pe = pefile.PE(args.image, fast_load=True)
    image_base = pe.OPTIONAL_HEADER.ImageBase
    for section in pe.sections:
        if not section.Characteristics & 0x20000000:  # IMAGE_SCN_MEM_EXECUTE
            continue
        start = image_base + section.VirtualAddress
        code = section.get_data()
        # A linear Capstone pass can stop or lose alignment in mixed code/data
        # sections. Scan every byte for x86 rel32 call/jump encodings instead.
        # The result may include rare data false positives, so inspect each hit.
        for offset in range(len(code) - 4):
            opcode = code[offset]
            if opcode not in (0xE8, 0xE9):
                continue
            address = start + offset
            destination = address + 5 + struct.unpack_from("<i", code, offset + 1)[0]
            if destination == args.target:
                mnemonic = "call" if opcode == 0xE8 else "jmp"
                print(f"{address:08X}  {mnemonic:<5} 0x{destination:x}")


if __name__ == "__main__":
    main()
