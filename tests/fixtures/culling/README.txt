Small x86 visibility routines from Steam Bayonetta.exe, timestamp 591AD330.
check.bin: VA 0051AF40, 183 bytes, cViewFrustum::check (no external references).
shadow.bin: VA 0051B000, 216 bytes, cViewFrustum::checkShadow (no external references).
wrapper.bin: VA 00AAA720, 31 bytes, cCameraFrustum::VfrustumCk.
The wrapper's relative call targets check.bin when mapped at original RVAs.
Tests execute these routines to verify ABI, hook relocation, scope and restoration.
