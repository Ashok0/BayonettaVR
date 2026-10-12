Steam Bayonetta.exe timestamp 591AD330, small x86 camera-routine fixtures.
look_at.bin: VA C5A160, 607 bytes. Camera update C4ED80 calls C5A420,
which jumps here. Builds a right-handed view (eye minus target).
projection.bin: VA C5A580, 176 bytes. Camera projection update AAB900 calls
this routine. Its homogeneous Z coefficient is -1 (RH projection).
The test relocates absolute constant references and calls to the same installed
D3DX9_43 and MSVCR100 exports used by the game. Camera code itself is unchanged.
This reproduces the determinant -1 camera recovered through the mod's LH
projection, a case the original .55 calibration tests did not include.
