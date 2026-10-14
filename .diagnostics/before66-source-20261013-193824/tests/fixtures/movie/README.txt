Original Steam Bayonetta shader.dat video conversion vertex/pixel shaders.
The regression renders the original Y/U/V shader pair with a known video
pattern and the game's orthographic movie rectangle. Each eye's output is
checked against independently intersected rays through an asymmetric headset
projection onto the fixed screen at z=3m, across yaw, pitch and head translation.
