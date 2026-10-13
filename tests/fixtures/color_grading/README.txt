Original pixel shaders from Steam Bayonetta data13.cpk / shader.dat.
Extracted by tools/extract_color_grading_fixtures.py from shader_unpacked.dat.
filter00.pso: 760 bytes, CRC32 6E722DE9.
filter04.pso: 500 bytes, CRC32 373A4BB7.

Both use c184.rgb for tint and c185.rgb for per-channel saturation offsets.
filter00 also adds a vertical color haze with c186.rgb.
Neutral values are 1,1,1 / 0,0,0 / 0,0,0 respectively. All .w components
remain unchanged so alpha and its blend behavior are preserved.
The original s1 tone table and native shader instructions remain active.
GPU tests compare independent tone-only RGB expectations and native alpha,
including nonlinear tone tables, both shader variants and both eye inputs.
