"""Check menu call sites against the supported Steam executable, without patching it."""
from pathlib import Path
import re
import struct
import sys
import pefile

path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('build/analysis/Bayonetta.exe')
pe = pefile.PE(str(path), fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x591AD330, 'unsupported executable'
image = pe.get_memory_mapped_image()
source = Path('src/D3D9Hooks.cpp').read_text()
install = source.split('bool InstallMenuHooks(unsigned char* image) {', 1)[1]

def values(name):
    body = re.search(r'const uintptr_t ' + name + r'\[\] = \{([^}]+)\}', install).group(1)
    return [int(value, 16) for value in body.split(',')]

sites, targets = values('sites'), values('targets')
assert len(sites) == len(targets) == 9
for site, target in zip(sites, targets):
    assert image[site] == 0xE8, f'not a direct call at {site:08X}'
    actual = site + 5 + struct.unpack_from('<i', image, site + 1)[0]
    assert actual == target, f'wrong call target at {site:08X}'
for site in (0xE4A0D, 0xE4CD7):
    assert site in sites
    assert image[site-5:site] == bytes.fromhex('B9 78 DC A7 05'), 'wrong early UI list'
# Both sites use the same list, on opposite sides of the native 0x10 test.
assert image[0xE49E9:0xE49F2] == bytes.fromhex('F6 05 54 DE 1A 05 10 75 20')
assert image[0xE4CB8:0xE4CBC] == bytes.fromhex('A8 10 74 3F')
assert '&HookEarlyIdList,&HookEarlyIdList' in install
print('Menu hooks: 9 Steam call targets, early UI list identity and mutually exclusive branches verified.')
