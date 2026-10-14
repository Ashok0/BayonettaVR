Small x86 routines from Steam Bayonetta.exe, timestamp 591AD330.
look_at.bin: VA C5A160, 607 bytes. Native RH camera view builder.
projection.bin: VA C5A580, 176 bytes. Native RH perspective builder.
trans_mesh.bin: VA 4A6990, 1328 bytes. Native model queue builder.
The hook test executes its empty-model path with the real prologue and
ret 4 epilogue, and checks ESP plus nonvolatile registers over 1000 calls.
Instrumented populated models check forwarding of the stack camera pointer
and mesh flags with first person on/off, other models and auxiliary cameras.
The verifier compares this fixture and the camera-load/caller/ret bytes
directly with the installed Steam executable.
FirstPersonGameCameraFixture relocates their constants and runtime calls.
The expected first-person view is independently constructed by these native
routines, then compared with the mod's camera replacement in both eyes.

Head anchor evidence: gameCamTypeFPS at A92B20 calls cModel::getPartsPtr(4)
at A92C36 and adds 0.1 along the player root's positive Z axis.
Head mask evidence: cModel::transMesh at 4A6990 checks mesh frame flags
(mesh+70)->10 against 04000001 for normal, extra-material and shadow passes.
Mesh names were read from the installed data/pl/pl0000.dat, pl0010.wmb.
No game assets are modified by the runtime implementation.
