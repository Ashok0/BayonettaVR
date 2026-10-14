"""Verify first-person addresses and anchor instructions against the Steam PE."""
import sys
from pathlib import Path
import pefile

pe = pefile.PE(sys.argv[1] if len(sys.argv) > 1 else 'build/analysis/Bayonetta.exe')
assert pe.FILE_HEADER.TimeDateStamp == 0x591AD330
assert pe.FILE_HEADER.Machine == 0x14c
data = pe.get_memory_mapped_image()
base = pe.OPTIONAL_HEADER.ImageBase
checks = {
    0x4A41C0: '53 56 8b f1 57 8d 7e 10',
    0x9CDA00: '55 8b ec 83 e4 f0 0f 57 db',
    0x499FC3: '81 be 00 08 00 00 00 02 00 00',
    0x49A012: '89 1c 8e ff 86 00 08 00 00',
    0x9CDBA9: 'f3 0f 10 86 54 4b 09 00',
    0x9CDBB7: 'f3 0f 10 8e 58 4b 09 00',
    0x8E6C7E: '0f 2f 05 dc d3 e5 00',
    0x4A6990: '55 8b ec 83 e4 f0 83 ec 64 53 56 57 8b f9',
    # cModel::transMesh(cCamera*): stack camera argument, callee cleanup,
    # and an actual caller that pushes the camera returned by the manager.
    0x4A69EE: '8b 5d 08 8b cb e8 d8 4e 60 00',
    0x4A6EB7: '5f 5e 5b 8b e5 5d c2 04 00',
    0x4A6ED2: '50 8b ce e8 b6 fa ff ff',
    0x4A4DC0: '8b 44 24 04 56 8b f1 85 c0 79 0b',
    0xA92C2E: '8b 8f 30 11 00 00 6a 04 e8 85 21 a1 ff',
    0xAAA2C0: '8b 41 08 c3',
    # Native player-camera behavior checks CAM_MODE_PL (1), not MAIN (0).
    0xA95CAC: '83 be 78 07 00 00 01',
    # cGameObj parent getter and live bone attachment used by weapon postMove.
    0x451AC0: '8b 81 00 0a 00 00 c3',
    0x454B16: 'f6 86 08 0a 00 00 01',
    0x454B25: '8b 8e 00 0a 00 00',
    0x454B5C: '8b 96 04 0a 00 00 52 e8 58 02 05 00',
    0x454B6C: '89 86 20 01 00 00',
    0xA23803: '8b 86 70 03 00 00 3d 20 00 03 00',
    0xA2390A: '8b ce e8 ff 11 a3 ff',
    # Steam fixed mesh and shader parameter layout (PC buffers are 32 bytes).
    0x4A0057: '8b 43 6c 89 4c 24 08 39 88 98 00 00 00',
    0x4A0071: '8b b0 9c 00 00 00',
    0x4A00D2: '83 44 24 10 60',
    0xC5DA05: '8b 06',  # SetIndices takes the buffer at +0.
    0xC6F300: '8b 41 0c c3',  # Native index count.
    0xC6F310: '8b 41 04 c3',  # Native StartIndex (no byte conversion).
}
for va, hex_bytes in checks.items():
    signature = bytes.fromhex(hex_bytes)
    assert data[va-base:va-base+len(signature)] == signature, hex(va)
fixture = Path('tests/fixtures/firstperson/trans_mesh.bin').read_bytes()
assert fixture == data[0x4A6990-base:0x4A6EC0-base]
print('First person: Steam model hook camera argument/ret 4/caller, native mesh fixture, bone accessor, native head-4 anchor and active-camera getter verified.')
import struct
assert struct.unpack_from('<f',data,0xDF9918-base)[0]==300**2
assert struct.unpack_from('<f',data,0xE5D3DC-base)[0]==900**2
print('Exploration hooks: root matrix builder, native combined-input axes, enemy-list layout, deadzone=300 and run threshold=900 verified.')
