Original Steam Bayonetta espshadersoftpt shaders from data13.cpk / shader.dat.
original.ps.bin: 552 bytes, CRC32 E166CF17
original.vs.bin: 388 bytes, CRC32 4A483974

The vertex shader stores per-vertex divided NDC in TEXCOORD1.xy. The original
pixel shader uses that perspective-interpolated value to sample scene depth.
The regression uses these unmodified binaries, tilted planes, checker-pattern
scene depth, asymmetric eye projections and viewport offsets to reproduce
misplaced depth masks. It compares the actual corrected alpha to the expected
depth occlusion at each rasterized pixel, rather than to another shader doing
the same implementation.
