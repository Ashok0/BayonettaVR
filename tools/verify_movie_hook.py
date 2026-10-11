"""Verify MovieDraw identity, queue layout and all UI cleanup call sites."""
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
# The queue reader depends on these exact bucket/bank/group loads, bank stride,
# and the head pointer at bucket + 0. MovieDraw initializes with 1 x 5 buckets.
assert pe.get_data(0x8706E0, 20) == bytes.fromhex('83 EC 10 8B 41 20 8B 51 28 53 8B 59 24 4B 55 83 E3 01 33 ED')
assert pe.get_data(0x87070A, 16) == bytes.fromhex('8D 44 5B 06 8D 3C C1 89 54 24 10 8B 17 8B 34 EA')
assert pe.get_data(0xE37C1, 7) == bytes.fromhex('6A 01 68 B8 DB A7 05')
assert pe.get_data(0xE2AB7, 2) == bytes.fromhex('6A 05')
queue = (pathlib.Path(__file__).resolve().parents[1] / 'src/MovieDrawQueue.h').read_text()
assert 'buckets!=1 || groups!=5' in queue
print('movie hook: Steam timestamp, MovieDraw list identity/queue layout and all 7 call targets verified')
