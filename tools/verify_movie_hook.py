"""Verify .48's MovieDraw call site and every existing UI cleanup call."""
import pathlib
import re
import struct
import sys
import pefile

pe = pefile.PE(sys.argv[1])
assert pe.FILE_HEADER.TimeDateStamp == 0x591AD330
source = (pathlib.Path(__file__).resolve().parents[1] / 'src/D3D9Hooks.cpp').read_text()
install = source.split('bool InstallMenuHooks(unsigned char* image)', 1)[1]
def array(name):
    text = install.split(f'const uintptr_t {name}[] = {{', 1)[1].split('};', 1)[0]
    return [int(n, 16) for n in re.findall(r'0x[0-9A-F]+', text)]
sites, targets = array('sites'), array('targets')
assert len(sites) == len(targets) == 7 and sites[-1] == 0xE5311
for site, target in zip(sites, targets):
    code = pe.get_data(site, 5)
    assert code[0] == 0xE8
    assert site + 5 + struct.unpack_from('<i', code, 1)[0] == target
# The this pointer is the MovieDraw OT, not either neighboring UI list.
assert pe.get_data(0xE530C, 5) == bytes.fromhex('B9 B8 DB A7 05')
assert pe.get_data(0x9F8F78, 10) == b'MovieDraw\x00'
print('movie hook: Steam timestamp, MovieDraw list identity and all 7 call targets verified')
