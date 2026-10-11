// espshadersoftpt.pso: retain the original color and depth fade. Sample scene
// depth at the rasterized pixel, not perspective-interpolated per-vertex NDC.
// c223 maps VPOS to the active viewport's texture coordinates (pixel centers).
ps_3_0
def c0, 0.5, -0.5, 1, 0
dcl_texcoord v0
dcl_texcoord1 v1.xyw
dcl vPos.xy
dcl_2d s0
dcl_2d s1
mad r0.xy, vPos, c223, c223.zwzw
texld r0, r0, s1
add r0.w, -r0.x, v0.z
add r0.w, r0.w, c0.z
rcp r0.w, r0.w
mov r0.x, v0.w
mad r0.w, r0.x, r0.w, -v1.w
rcp r0.z, c185.x
mul_sat r1.w, r0.w, r0.z
texld r0, v0, s0
mul r0, r0, c184
mul oC0.w, r1.w, r0.w
mov oC0.xyz, r0
