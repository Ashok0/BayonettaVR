# BayonettaVR

Experimental native-stereo 6DOF OpenVR mod for the 32-bit Steam release of
Bayonetta (executable timestamp `0x591AD330`).

## Confirmed red-streak fix (build 20261013.65)

The user confirmed on **2026-10-13** that build **.65** fixes the red streaks
that shifted with HMD movement in the maroon-background combat/practice areas.
`DisablePracticeReflection=1` suppresses the red-tinted reflection card seen
at draw 529 in both `20261013_000226_1` and `20261013_010557_1` captures.
**Alt+F1** toggles suppression for comparison. This setting defaults on when
absent from the INI; set it to `0` and restart to restore the original card.

Matching requires the exact native `espshadershimmer.pso` shader (CRC32
`DD5F8DF1`, 476 bytes), two primitives, the captured (.5,.2,.2,1) material,
zero distortion with (-.5,-.5) offsets, the same 4x4 DXT5 texture in stages
0 and 2, a viewport-sized reflection render texture in stage 1, and the
captured depth/blend states. Only native VR scene draws are eligible. Ordinary
particles, other shimmer materials, UI and flat movie composition pass through.
No camera transforms, game shader objects or shader constants are modified.

The native-shader GPU regression checks the captured signature, negative
matches, state-block binding, reset, unchanged bytecode/constants and 8,192
pixels with suppression on/off. Extract its fixture with
`tools/extract_practice_reflection_fixture.py` and run
`tests/build_practice_reflection_checks.cmd`.

Release/Win32 and the full regression suite pass. Build .65 was installed
with SHA256 verification; existing INI settings were preserved. The missing
`DisablePracticeReflection` key uses the enabled default.

The .64 capture completed, but color targets are standalone surfaces whose
`GetContainer(IDirect3DTexture9)` fails; its scene checkpoints cover only the
texture-backed depth pass (draws 173-343). Thus the reflection diagnosis uses
the shader, matrix, material and texture evidence, not a claimed before/after
color checkpoint. The capture and original eye images are preserved under
`.diagnostics/streak64`.

## Red-strip investigation (build 20261013.64, diagnostic only)

The expanded .63 capture `BayonettaVR_scene_20261013_000226_1` contains all
effect shader identities, but its sixteen scene checkpoints were exhausted
by draw 269, before the main effect passes. The user reports that toggling
the particle-depth correction with F1 makes no difference to the strip.
Build .64 was diagnostic only; the subsequent .65 suppression is confirmed
to fix the reported streaks, as documented above.

F12 now saves a 256x144 GPU scene checkpoint before each eligible draw,
up to the existing 2,048-draw cap. Readbacks still happen after the stereo
pair. Texture previews are capped at 64 and multiply RGB by alpha to reveal
alpha-only effects; scene previews preserve RGB regardless of target alpha.
This explicit diagnostic can briefly pause rendering and save about 300 MB
at the limit. It does no checkpoint work during ordinary play.

Release/Win32 and the capture GPU regression pass. Tests cover the final
draw checkpoint, both caps, alpha-only textures, scene RGB with zero alpha,
and restoration of shader constants, bindings, MRT images and render state.

## Startup display (build 20261012.63)

The companion submits black frames while waiting for the first game frame,
instead of automatically displaying the cyan test grid. **F5** still toggles
the grid manually. The first game frame retains its compositor discontinuity
flag when switching from the startup display. The expanded F12 diagnostic
from .62 remains available; the red-streak fix is documented above in .65.

Validation: Release/Win32 build, two-process GPU transport checks (including
resize, device recreation, pose integrity and host lifecycle), and ten grid
render/readback cases passed. Headset startup appearance awaits confirmation.

## Sepia/color-grading removal (build 20261012.61)

`DisableColorGrading=1` removes the post-processing tint, desaturation and
additive color haze. Press **Ctrl+Shift+F1** while playing to compare with
the original grading. Set the INI option to `0` and restart to keep the
original look by default.

The bypass recognizes only the exact Steam `filter00.pso` (CRC32 `6E722DE9`,
760 bytes) and `filter04.pso` (`373A4BB7`, 500 bytes). During their draws it
neutralizes the RGB components of constants c184-c186, then restores them.
The tone-table lookup, native alpha, blend state and game-owned shaders
remain intact. This removes special-effect color tints that use these same
grading controls too; baked texture colors and video colors are unaffected.

`tests/build_color_grading_checks.cmd` runs both native shaders on the GPU:
24 cases / 24,576 pixels check tone-only RGB, exact alpha preservation,
nonlinear tone curves, both eye inputs, toggle restoration, state-block
bindings, reset, exact fingerprints and unchanged shader bytecode.
This source build is based on the current .57 checkout.
Release/Win32 and the full regression suite pass. Installed with
`DisableColorGrading=1`; existing settings were preserved. The replaced .60
binaries, INI and log are in `.diagnostics/before61-20261012-225512`.
The installed files match the tested build:

- d3d9.dll SHA256: `9C3A4208BCAB08CDEA78918FA1E1953A48238C63011AC171ECC3FC4B8FBD96B2`
- BayonettaVRHost.exe SHA256: `3DC596C0A1B481F6ACA54A09A7AD2EA8DBA0D1B89A84C579EB3134169A3C2E12`

The final appearance in the headset remains to be checked in-game.

## Level headset recentering (build 20261011.57)

Hold **L3+R3 for one second** on the gamepad or both VR thumbsticks to reset
headset heading and clear tilt captured in the tracking origin. F8 performs
the same reset. Startup also uses a level origin, so a headset tilted during
launch cannot permanently tilt the game. Actual head pitch/roll continue to
track normally after recentering.

The stored origin contains horizontal heading and headset position only.
There is no game-camera leveling, floor reference or camera-height offset.
The .54 scene projection path is restored exactly; analog-stick camera orbit
and elevation remain controlled by the game. This replaces the withdrawn
.55/.56 floor-calibration behavior. Their FloorCalibration INI key has no
function in .57. No special ground position or standing posture is required.

Gamepad and VR-controller shortcuts share the one-second hold policy and
cancel on focus loss, disconnect or stale input. Gamepad recentering also works
with VRControllers=0. Recenter changes take effect together for both eyes.
Tests cover 90 tilt/recenter/height cases, including 80 reproductions of the
old persistent-launch-tilt behavior, and 100 stereo camera orbits with original
camera position, pitch and yaw preserved. Withdrawn floor code is archived in
`.diagnostics/heading57/withdrawn56`.

Release/Win32 and the full regression suite pass. Build .57 is installed;
installed binaries match the build outputs by SHA256:
- d3d9.dll: `ACA56E0993A136776795DD4287890E5DCBEE404D634F640A42EFBBE8801C426A`
- BayonettaVRHost.exe: `507266BFBDAC5A8AEA307E83112066BCFB7BA6414B8D43650E8B6EF2F94CF9FD`

Previous binaries and INI are backed up in
`.diagnostics/before57-20261011-230739`. The obsolete FloorCalibration stanza
was removed from the installed INI; other settings were preserved. Headset
appearance and physical-controller behavior still need confirmation in game.

## Camera visibility for native VR (build 20261011.54)

`FixCameraCulling=1` expands scene visibility around the game camera, so head
turns can see geometry behind or beside its original view. This addresses the
reported camera-dependent pop-in independently of Bayonetta's facing direction.
The ordinary `cCameraFrustum::VfrustumCk` at Steam VA `00AAA720` is hooked;
its caller uses the camera at manager `05B29050 + 8`. The override is restricted
to that active camera and the main camera's frustum at `00F2E0D0`, while native
VR is active. The separate shadow check and unrelated cameras retain their
original behavior. The existing map cinema fix remains enabled.

The original check runs first. Rejected bounding spheres are admitted within
a sphere enclosing the camera's far frustum corners, including object radius,
in every orientation. This preserves all originally visible objects and a
finite distance limit, removing the old forward/near/side-plane rejection for
surrounding geometry. Invalid range inputs fall back to the original result.
The hook validates the executable timestamp, full wrapper bytes and camera
getter, and restores its original instructions at shutdown.

This draws additional geometry and may cost frame time. Set `FixCameraCulling=0`
and restart to compare the original behavior. Logs report installation and the
first off-camera object retained. Tests execute the actual x86 game routines,
checking the trampoline, 360-degree sweeps, 100,000 visibility comparisons,
far range, camera switching, disabled fallback, shadow isolation and removal.
Headset appearance, frame cost and any additional map-specific visibility
systems still require an in-game replay; the isolated tests cannot prove those.
Release/Win32 and the full regression suite pass. Build .54 is installed with
`FixCameraCulling=1`, preserving the other installed settings. Installed SHA256:
`C287A03BD564FD3E3865AD22A8F308B32824192C5BABC40BC641C49E95E2EB45`.
The previous DLL, INI and log are backed up under
`.diagnostics/before54-20261011-154149`.

## Native map sequence on a fixed screen (build 20261011.53)

The 11:54:49 capture identifies the native map asset: a 4096x2048 DXT1
texture whose 256x144 preview has CRC32 `7A6D8996`. This sequence queues no
MovieDraw commands, so .50's movie-only selection never composed it onto a
screen. .53 recognizes the actual map pixels and uses the existing complete
cinematic composition path for these frames. The map, animated cards, scene
geometry and overlays share one fixed screen, with black surroundings in both
eyes. This deliberately flattens this authored sequence; it is not a repair
that retains the surrounding native 3D scene and moves only the face.

`WorldLockedMapSequence=1` is the default (set it to 0 to keep the prior native
rendering). Dimensions/format only select candidates; they cannot activate
cinema without the pixel fingerprint. At most four new candidates are sampled
at a draw-list boundary, with the result cached on each texture object. There
is no recurring readback for a recognized asset. Sampling failure or a changed
asset fails closed to native rendering. The entering frame restores effect
streams and recomposes before either eye is published; UI cleanup runs once.
When the map stops being drawn, the completed frame remains on the screen and
the following frame returns to native stereo. Device resets release resources.

F12 now retains texture references at draw time and performs all preview draws
after both eye captures, avoiding nested diagnostic draws inside engine draw
callbacks. Static map/card previews remain useful; render-target previews are
explicitly labeled as end-of-pair contents. The .52 eye capture was distorted,
so it is not evidence of the face's exact geometry. The map-screen routing
addresses the sequence as a complete composition rather than deleting a draw.

Release/Win32 and the complete regression suite pass. The new GPU test checks
the captured reference fingerprint, a same-size DXT1 nonmatch, entry, 100 cached
frames, exit, and preserved MRT pixels/bindings and viewport/scissor state.
Existing movie tests verify both eyes, moving overlays and black rear views.
The actual .53 map replay and headset appearance still require user validation.
The installed DLL matches the build, SHA256
`CD02CEC8CD99565CCC3620AADE8F54BA50B8C1B5CE8BB3D8ACB86664DDA67D44`.
The .52 DLL, INI, log, eye pair and scene trace are preserved under
`.diagnostics/face52-20261011-115449`.

## Scene trace MRT correction (build 20261011.52)

The 11:09:53 F12 capture from .51 contains 300 native draws, both eye images,
and five texture previews. The eyes show the coastal city, map edge and a
very large nearby surface; the face's texture is not yet identified. Three
previews taken during the depth/normal pass are black. Evidence, the .51 DLL,
INI and log are preserved in `.diagnostics/face51-20261011-110953`.

The preview renderer left the engine's secondary render target attached.
An A/B GPU regression reproduces black previews with that binding retained
and valid texture pixels with it detached. .52 explicitly saves, detaches and
restores all supported extra render targets; D3DSBT_ALL does not save them.
The test verifies both target bindings and both complete scene images remain
unchanged. This fixes the diagnostic, **not the reported head panel**.

F12 also includes low-polygon orthographic textures and metadata for draws
without a recognized camera matrix (`matrix=c-1`, identity placeholder).
The limits remain 2,048 draws and 16 previews of 256x144 pixels. Another F12
capture while looking directly at the face is needed to identify its texture.
Release/Win32 and the complete regression suite pass. The installed .52 DLL
matches the build, SHA256
`B696F61496C38418241D59B651CD4C6C997796224F564D5880DB95DC104274E2`.

## Native scene-card investigation (build 20261011.51)

The user confirmed the airplane animation works in .50, but a giant face card
remains behind the viewer during the map/airplane story sequence only. The
10:34 F12 pair and log show native scene rendering with an empty Id/IdFont
capture, rather than .50's complete movie-frame path. The saved view contains
the map and surrounding scene geometry; it does not identify the face's draw.
This issue is **not yet fixed**. .51 preserves .50's rendering behavior and
adds a bounded diagnostic for the missing native scene evidence.

F12 now saves the existing eye pair plus a `BayonettaVR_scene_...` directory.
It records at most 2,048 left-eye draws (shader CRCs, original projection,
render target, texture identities, depth/blend/cull state) and previews at most
16 distinct color textures from low-polygon perspective scene draws. Previews
are 256x144; GPU copies occur at use and CPU readbacks occur after both eyes.
There is no full-resolution texture dump or per-draw CPU readback. Capture
automatically disarms after one frame. Shift+F12 additionally retains the older
full-size menu diagnostic, which is unnecessary for this issue.

To collect the remaining evidence, look directly at the face in the map
sequence and press F12, then quit normally. The trace's GPU regression checks
its limits, sampled preview pixels, and unchanged scene image, render target,
depth, texture binding, viewport, scissor, culling and vertex layout.
Release/Win32, the full regression suite, and the strengthened in-scene trace
test with live shader objects pass. .51 is installed with SHA256
`0F8ACD2231BEFC66C1E56DE7DC0D8F2F0CFD1352745B6163BA02D8269040FEF3`.
The .50 DLL, INI, log, F12 eye pair and shader files are preserved under
`.diagnostics/face50-20261011-104836`.

## Composed movie cinematics (build 20261011.50)

The user reported a story cinematic with a visible map but missing airplane
animation, plus a giant face card visible behind the viewer. The .49 renderer
separated MovieDraw from scene geometry and Id/IdFont overlays; those layers
therefore did not share the movie's projection, depth or composition.

.50 detects queued MovieDraw commands before rendering, using the verified
Steam OT render-bank layout. For those frames the engine composes its complete
original view once, including animated geometry, video, overlays, subtitles
and fades. A GPU copy preserves the final composition, which is then projected
onto the same fixed screen for each eye. Each eye's surrounding image is
cleared to black, preventing off-camera cinematic cards from appearing around
or behind the screen. Cleanup runs once. An empty movie queue immediately
returns to native stereo and the existing HUD/menu path. Screen dimensions,
recenter behavior and the .39 visual preferences are retained.

The movie GPU checks cover 89,359 composition pixels, three moving-overlay
positions, subtitles, asymmetric eye projections, six completely black rear
views, queue-bank transitions, viewport/scissor restoration, and rendering
inside the engine-owned D3D scene. The existing 36 movie projection cases and
full regression suite also pass. The specific story map/airplane scene still
requires an in-headset replay; the regression uses a synthetic layered scene.
The final installed build completed 9,600 live startup frames with successful
movie copies and both eye captures, with no movie-render errors in the session
log (`.diagnostics/movie50-startup`). An initial live probe caught nested
BeginScene calls; they were removed and the test now exercises the engine-owned
scene contract. Desktop visual verification was unavailable because the
Computer Use native pipe could not connect. The installed DLL SHA256 matches
the release build; the preceding .49 DLL/INI/log are backed up under
`.diagnostics/before50-20261011-101738`.

## Stereo splash movies (build 20261011.49)

The user reported SEGA and Platinum Games splash movies visible only in the
left eye, while CRIWARE was correct. Startup eye-buffer captures reproduced
the missing right-eye SEGA image and corrupted/blank right-eye Platinum image.
Both movie draw calls executed with depth testing disabled, so .48's draw
counts alone did not establish that the decoded movie survived the second draw.

.49 executes the dedicated movie list once per stereo frame, into one RGBA8
render target, and replays that same image on the world-space screen for each
eye. It no longer reruns the game's movie renderer for the right eye. The
decoded image is captured before applying the HMD transform. Empty movie passes
produce no screen, so CRIWARE, menus and gameplay are not covered by a stale
movie. The capture uses about 14 MiB at 2560x1440 and no CPU readback. Device
reset and resizing release/recreate its resources. The .48 movie size/distance
settings and recenter behavior are retained.

After-fix startup captures visibly contain SEGA and Platinum in both eyes,
including the animated Platinum splash. The GPU test invalidates the decoder
inputs after capture and checks both eye views over 36 turn/lean cases (22,227
pixels against independent rays). Shader, texture and matrix restoration,
empty passes and resource recreation pass. Temporary automatic startup
captures used for verification are removed from the installed build.
Release/Win32 and the full regression suite pass. The final installed DLL
matches the build; before/after eye captures are preserved under
`.diagnostics/splash48-before` and `.diagnostics/splash49-after`.

## World-space movies (build 20261011.48, renderer superseded by .49)

The user confirmed .47 fixes the fog silhouettes. The next reported issue is
double vision in the first film-strip intro after launching the game.

The native `MovieDraw` list runs separately from the captured Id/IdFont UI.
Previously its orthographic rectangles bypassed HMD projection and filled
each eye's texture identically, despite the headset's asymmetric projections.
.48 hooks only this verified call at Steam RVA `0xE5311` and projects movie
rectangles onto one recenter-space screen using each eye's actual view and
projection. Movie decoding, shader objects, texture coordinates, color and
playback timing are unchanged. No new capture textures or frame replay are
needed. The ordinary matrix/scissor restoration path restores the original
state after each movie draw. Movie transforms bypass the scene/HUD matrix cache
because the movie screen has independent dimensions.

`WorldLockedMovies=1`, `MovieWidthMetres=4.0`, `MovieDistanceMetres=3.0` are the
defaults. The screen stays fixed during head movement; F8 or the existing
controller recenter action brings it back in front of the user. Set
`WorldLockedMovies=0` and restart to restore the previous movie behavior.
This targets full-screen movie playback; menu previews retain the menu path.

The film-strip border is baked into `data/movie/syukyaku_A.sfd` (confirmed by
decoding a frame directly from that file). It is retained so the complete
picture remains visible; removing it would require cropping or altering video
content. The fixed screen reduces its apparent size and keeps both eyes aligned.

The original game movie shaders pass 36 GPU eye/turn/lean cases with 29,888
pixels checked against independent world-space rays. The old unprojected path
mismatches 15,028 of those pixels. The original matrix is restored after every
draw. `tools/verify_movie_hook.py` validates the executable timestamp, movie-list
identity and all seven movie/UI cleanup call targets. In-headset confirmation
of .48 is still required.
Release/Win32 and the full regression suite pass. The installed .48 startup run
reached 1,200 stereo frames and logged 600 projected movie draws in each eye at
matrix c24. The installed DLL hash matches the build. This confirms the movie
path is active, independently of the remaining headset appearance check.

## Soft-particle depth alignment (build 20261011.47, user confirmed working)

The user confirmed the silhouette persists with .46. Its log shows the SSAO
bypass active throughout the cutscene; the earlier motion-blur and SSAO
workarounds have not resolved this artifact.

The game's `espshadersoftpt` vertex shader divides screen XY by W before
interpolation. This produces a distorted depth lookup when VR head rotation
tilts its originally camera-facing particles. .47 samples scene depth at the
actual rasterized pixel (`VPOS`) instead. It retains the original particle
color, alpha/fade calculation, texture slots and geometry. The VR projection
also preserves the game's depth encoding so its depth reconstruction constants
remain valid. HUD and reference-grid projections retain their own depth range.

`FixSoftParticleDepth=1` enables the correction. **F1 toggles the particle depth
lookup correction**, Ctrl+F1 toggles the previous SSAO bypass, and Shift+F1
toggles the previous motion-blur bypass. F1 does not change the corrected scene
depth encoding. Restart restores the INI settings. Existing visuals, DOF
suppression and controller mappings remain configured as before.

Only the exact Steam pixel shader (`E166CF17`, 552 bytes) is recognized. Game
shader creation and bytecode remain intact; a preassembled substitute is lazily
created on its first VR scene draw, bound for that draw, then restored along
with its temporary constant. Actual bindings are queried to cover state blocks.
Device reset releases the substitute. This correction performs no frame capture
or readback.
The log reports corrected draw counts separately for both eyes.

The real game VS/PS regression reproduces 25,100 incorrectly placed mask pixels
with the original shader. The correction passes all 59,796 checked pixels in
36 GPU cases covering head yaw/pitch, asymmetric eyes and viewport offsets.
Toggle restoration, shader metadata, state blocks and recreation also pass.
36 mathematical depth cases and the full regression suite pass; Release/Win32
build succeeds. This proves the shader bug and its correction in the test scene;
the user subsequently confirmed that this fixes the opening-cutscene silhouette.

Regenerate `src/SoftParticleDepthBytecode.h` with a Win32-built
`tools/assemble_shader.cpp`, passing `src/shaders/SoftParticleDepth.asm`, the
output header path, and `kSoftParticleDepthBytecode` as its three arguments.

## Ambient-shadow bypass (build 20261010.46, ineffective for reported silhouette)

The user confirmed .44 loads again but the head-moving transparent shadow remains
in cutscenes. .46 is based on that working path; the .45 capture hooks remain out.
It targets the separate screen-space ambient-occlusion pass as a candidate
workaround. The user's artifact has not yet been confirmed against this pass.

`DisableAmbientOcclusion=1` removes ambient darkening from the exact Steam
`filtershaderssao.pso` (CRC32 `46871F68`, 8172 bytes). This shader also copies scene
color, so its draw must run. Temporarily setting its sample-count selector
`c198.y` to zero makes the occlusion multiplier one; the original constant is
restored immediately after drawing. No shader object or bytecode is replaced.
Only the identified shader is affected, and only while VR is active.

**F1 now toggles the ambient-shadow bypass**; Shift+F1 retains the earlier
motion-blur comparison. Restart restores both INI settings. The tradeoff is
loss of SSAO contact darkening. Ordinary shadow maps, menu blur, .39 colors,
DOF bypass and controller mappings retain their existing paths. Logs record
`ambient shadows: draws=... bypass=...` when the affected pass actually runs;
no heavy capture is required for this test.

The GPU regression uses the original game shader across 18 depth/sample-count/
eye cases. Bypassed output matches scene RGBA exactly, toggling restores the
original shading, and constants, shader metadata and state-block behavior pass.
The baseline darkened 12,288 pixels, confirming the test exercises active AO.
This validates the bypass mechanism; it does not yet establish that SSAO is the
source of the user's haze.
Release/Win32 and the full regression suite passed. A Steam launch of .46
(PID 19128) reached 300 stereo frames and logged 600 ambient-shadow draws with
`bypass=1`, confirming this pass is active and the override is being applied.
The opening-cutscene appearance remains unverified in-headset.

## Loading hang: .45 withdrawn, .44 restored

After .45, the user reported a loading-screen hang. The latest log stops after
frame 1,800 and contains no scene-capture start; no new scene-capture directory
was created. This does not establish the cause. The exact backed-up .44 DLL
that previously reached the graveyard was restored, and .45's scene-capture
integration was removed from the source. The standalone capture helper/test
remain for investigation, but are not included in the mod. Settings are retained.
Loading past the reported hang still needs confirmation; the haze is unresolved.

## Cutscene haze investigation (withdrawn build 20261010.45)

The user's .44 F1 comparison made no difference. The 17:12 F12 capture shows
the graveyard cutscene with zero captured menu draws, and no motion-blur
suppression events occurred during that run. The targeted motion-blur passes
were not active. The source of the head-moving shadow/haze is still unresolved.

F12 now additionally records one left-eye scene frame into a timestamped
`BayonettaVR_scene_*` directory next to the game. It saves actual bound vertex
and pixel shader assembly, original camera/pixel constants, draw order,
render-target and texture bindings, sampled render textures, completed targets,
and images before/after small textured draws. Floating-point buffers also get
DDS copies so near-black depth values remain available. Unlike the old menu-only
trace, these images are read before their backing textures can be reused.

Capture is limited to 8,192 draws, 80 images and a 512 MiB conservative raw-image
budget. It may briefly pause rendering while saving. No new effect is disabled
in .45; it is a diagnostic update, not a confirmed haze fix. The existing .39
colors, menu fixes, DOF bypass and controller mapping are retained.

The targeted GPU test checks PNG readback inside BeginScene/EndScene, preserved
shader identity, targets, samplers, camera constants, final pixels and completion.
Release/Win32 and the full regression suite passed. Steam startup of .45
(PID 28900) reached 1,200 stereo frames. The in-game F12 scene trace and haze
diagnosis still require the next cutscene capture.

## Camera motion-blur ghost bypass (build 20261010.44)

The reported faint, object-shaped haze moving with the headset predates the DOF
change. Inspection of the installed game's shaders identified six motion-blur
programs (five unique bytecodes) that reconstruct scene samples using
`g_InvOldWVP` and `g_InvNewWVP` pixel constants. Those matrices do not receive the
mod's vertex-camera HMD transform, making this a candidate for displaced ghosts.
The user's exact artifact has not yet been confirmed against this pass.

`DisableMotionBlur=1` bypasses only these identified motion-blur draws while VR
is active, preserving original shader objects and metadata as in .43. It does
not change menu blur, bloom, SSAO, or .39's colors. **F1** toggles the bypass for
a same-scene comparison; restart restores the INI setting. If the haze remains,
capture it with F12 and report whether F1 changed it. Logs record recognition,
suppression counts and toggle state. The tradeoff is removal of the game's
cinematic motion blur. No shader replacement is returned to the game.

GPU regression checks cover all five motion-blur bytecodes alongside the four
DOF shaders, original metadata, setting-off behavior, state-block bindings and
color/depth preservation in both eyes. Headset confirmation is still required.
Release/Win32 and the full regression suite passed. A Steam startup check
(PID 10992) loaded all six motion-blur programs and rendered over 1,200 stereo
frames without a startup crash. This does not validate the opening-cutscene haze.

## DOF startup-crash correction (build 20261010.43)

The user reported .42 crashing to desktop. Its log stopped immediately after
replacing the first DOF shader (`B959E4D7`), before frame rendering. The .41 DLL
was restored while investigating. No Windows crash dump was available; the exact
faulting instruction was not established. The standalone GPU tests did not
exercise the game's shader-loading path.

Build .43 retains each original shader object and its bytecode/metadata. The
creation hook only identifies the same four CRC/length pairs. During drawing,
the proxy reads the actual bound pixel shader and suppresses the draw if it is
one of those four and `DisableDepthOfField=1`. No replacement shader is compiled
or returned to the engine. Reading actual device state also handles bindings
restored through D3D9 state blocks. Other draws use the original path.

Updated GPU tests verify original bytecode preservation, state-block restoration,
setting-off behavior, unrelated shaders, and scene color/depth preservation in
eight eye cases. An actual Steam launch of .43 (PID 29404) loaded all four
identified shaders and continued rendering hundreds of stereo frames past the
.42 startup-crash point. This verifies startup, not cutscene appearance or
long-session stability; those still require headset testing.

## Disable cutscene depth of field (build 20261010.42)

`DisableDepthOfField=1` disables the game's four known DOF pixel-shader passes.
The exact bytecode CRC32 and length pairs were verified against `shader.dat`
extracted from the installed Steam `data13.cpk`: `B959E4D7` (4140 bytes),
`548C7346` (4496), `FC2A113C` (4608), and `CB1D0471` (2376). Identification is
credited to masterotaku's [optional Bayonetta DOF removal](https://helixmod.blogspot.com/2017/04/bayonetta.html).
That mod's DLL is not installed or chained into this mod.

The VR proxy replaces only those shaders with a discard shader that writes no
color or depth. Unmatched shaders remain original. Replacement failure also
keeps the original shader. Logs report `dof: disabled verified pixel shader`
with each matching CRC. Set `DisableDepthOfField=0` and restart to restore DOF.
This does not change .39's color/contrast or the menu blur used for labels, and
retains .41's controller support. Blur baked into video files is unaffected.

Validation: all four original shader fixtures match; eight GPU eye readback
cases preserve scene color and depth; setting-off, unrelated shader, near-match
and allocation-failure cases pass. The Release/Win32 build and existing VR
regressions pass. Cutscene appearance still requires an in-headset check.

## Oculus Touch as Xbox input (build 20261010.41)

`VRControllers=1` maps a pair of Oculus Touch controllers to Xbox 360 player one
inside Bayonetta. No virtual-controller driver is required. The game imports
XInput 1.3 GetState/SetState by ordinals 2/3; the mod patches those import slots,
supplies a pad when no physical controller is present, and merges physical input
when a gamepad is connected. Other Xbox slots retain their original behavior.
This build uses .39's visuals, as requested; the .40 image enhancement is absent.

| Touch control | Xbox input |
| --- | --- |
| Left / right thumbstick | Left / right stick |
| A, B, X, Y | A, B, X, Y |
| Left / right index trigger | LT / RT, analog |
| Left / right grip | LB / RB |
| Ordinary left / right stick click | L3 / R3 |
| Left grip + left stick click | Start |
| Right grip + right stick click | Back |
| Hold both grips and move right stick | D-pad, including diagonals |
| Hold both stick clicks for one second | Recenter camera and world-locked HUD |

Recenter uses the same origin update as F8, once per hold. The right System bit
also triggers it if delivered, but the installed Oculus profile does not expose
that button. SteamVR reserves System input for its dashboard and does not normally
deliver it to applications: [Valve's input documentation](https://github.com/ValveSoftware/openvr/blob/master/docs/Driver_API_Documentation.md#reserved-input-sources).
The both-stick gesture is the supported fallback; this mod cannot take over the
right Oculus/System button from Meta or SteamVR.

Input requires the game to have desktop focus and SteamVR to allow application
input. Dashboard/focus loss neutralizes Touch input; snapshots expire after
250 ms, and disconnects return to physical XInput. Both external-compositor and
legacy in-process modes are supported. Detection requires controller type
`oculus_touch` and SteamVR's standard Touch legacy bindings. Other controller
types are left unmapped. Physical controller rumble is forwarded; Touch haptics
are not implemented. Set `VRControllers=0` to disable the bridge.

The shared protocol is version 2: install the new DLL and host together. Logs
report XInput hook installation, detected controller types and recenter events.
Validation covers 29 mapping/state/gesture cases, a real ordinal-import hook
test, the existing rendering checks and two-process transport lifecycle checks.
Physical Touch button use and comfort still require in-game verification.

## Menu blur framebuffer copies (build 20261010.39)

The .38 F12 captures and the game's `cIdOtWork::drawBlur` path identify the
missing step in .35's menu capture: unhighlighted labels use a two-pass blur
with framebuffer snapshots between passes. Those copies read the engine's
cached scene target, bypassing the redirected UI target. The blur then overwrites
the letters with rectangular pieces of background. Highlighted labels skip it.

During menu capture, StretchRect snapshots now read the captured UI. Each scratch
texture has a matching white-background snapshot, substituted at any of the 16
pixel samplers during the white draw (the game's blur uses sampler 14). Sampler
bindings are restored afterward; unused companions are retired and device reset
releases them. World-locked placement and effect-stream replay remain in place.
F12 diagnostics now inspect all 16 pixel sampler slots.

Validation uses the actual horizontal and vertical blur shaders from the user's
F12 captures. The regression reproduces missing labels with the old copy path
and compares corrected glyph/background pixels against normal rendering for
both eyes, consecutive frames and resource recreation. Release/Win32 and the
regression suite pass. Actual main-menu and nested-menu appearance still needs
in-game confirmation.

## Menu regression diagnostics (build 20261010.38)

User testing found no visible improvement in .37. The user also clarified that
the black menu blocks began with .35's stereo-menu capture, before the separate
combat-effect fix. The actual menu regression is still unresolved; the synthetic
blend comparisons are not evidence that the game's label path is fixed.

F12 now records one native frame's actual Id/IdFont draw states, input textures,
vertex/pixel shaders and captured output layers, alongside the existing eye
screenshots. Diagnostics are written beside the game in a timestamped
`BayonettaVR_menu_*` directory, with texture/shader indices in `BayonettaVR.log`.
Capture is bounded to 160 draws, 28 input textures plus four output layers, and
32 shaders per type. Texture files reflect contents at the end of the stereo
pair; shader constants and stream bindings are logged at the individual draw.
It is inactive during normal play and may briefly stall when F12 saves files.

To collect the missing evidence, open the main menu with the blocks visible,
press F12, move the highlight to another option, press F12 again, then exit.
This build adds diagnostics without claiming another menu rendering fix.

## Menu label blending (build 20261010.37)

The user reported that unselected title-menu and nested-option labels disappeared
into black rectangles, becoming readable only when highlighted. The game's UI
blend table includes destination-color blending. A transparent-black capture
cannot retain that contribution, and forcing framebuffer alpha to act as panel
coverage also conflicts with RGB-only draws and the game's alpha masks.

Build .37 captures each UI primitive over black and white floating-point targets,
preserving the game's blend state and color-write masks. The difference retains
per-channel background transmission; compositing applies that transmission and
the emitted color to each eye's scene. Floating-point capture preserves additive
and subtractive values. Both captures maintain independent depth/stencil buffers,
and target switches restore viewport and scissor rectangles. Initial clears cover
the entire capture even if the game left scissoring enabled. Game-issued UI clears
are mirrored too. Game menu logic still runs once; only GPU draws are repeated.

The panel remains world-locked, and build .36's effect-stream replay is unchanged.
This adds a second capture per UI draw and uses more GPU memory. The reconstruction
targets the tested alpha, additive, subtractive and destination-color modes;
arbitrary nonlinear framebuffer effects are not guaranteed to match.

Validation: Release/Win32 build and `tests/build_checks.cmd` pass, including the
12 existing menu GPU cases and 32 direct-render-versus-panel comparisons covering
both eyes, unselected/highlighted labels, RGB-only writes, overlapping option
backgrounds, stencil masks and scissor clipping. The previous renderer fails the
new unselected-label case. Actual title and nested menus still need user validation;
Computer Use could not inspect them because its native pipe was unavailable.

## Stereo combat effects (build 20261010.36)

Several `cEspDraw` variants clear their embedded vertex-stream descriptors after
drawing. In native stereo, the left eye consumed those descriptors before the
right eye could draw the same geometry. This is a concrete cause of one-eye-only
effects and a likely explanation for the reported red slash.

Build .36 snapshots descriptors at 21 post-draw clear sites across seven effect
draw variants, lets the original clears run, then restores the first snapshot of
each descriptor immediately before the right-eye render. Repeated passes within
an eye still see the game's normal consumed state. The existing .35 end-of-frame
hook keeps the effect buffer heap alive until both eyes finish. Initializers and
allocation calls to the reset function are not patched. Signature checks verify
each CALL/tail-JMP and the reset function body before installation.

Replay uses bounded storage for 8,192 streams without render-time allocation;
overflow leaves excess streams on the original path and emits a warning.
Snapshots are discarded after restoration, on a new frame, and on device reset.
Logs report `effects: stereo stream replay installed` and cumulative
`effects: replayFrames` / `streamsRestored` counters. No settings change is needed.
World-locked UI capture, panel placement and compositor submission are unchanged.

Validation: Release/Win32 build, all 21 instruction signatures against the Steam
executable, and `tests/build_checks.cmd`, including 100 simulated stereo frames
with repeated passes, descriptor restoration, capacity overflow and reset.
The existing 12 UI GPU readback cases also pass. The red slash and other combat
effects still require in-headset confirmation; the user confirmed .35's UI works.

## World-locked HUD and stereo menus (build 20261010.35)

`WorldLockedHUD=1` places the game's HUD and menu passes on a stereo panel
fixed in tracking space relative to your last recenter. Turn your head to look
at the corners, or lean toward the panel to read it. **F8** places it in front
of your current pose again. It is anchored to the VR recenter origin, not to
Bayonetta's character or a location in the level. Normal full tracking is
required; F7's fixed-pose diagnostic intentionally disables native movement.

The default panel is 3.2 metres wide and 2 metres ahead. Change `HUDWidthMetres`
or `HUDDistanceMetres` to adjust apparent size, and `HUDVerticalOffsetMetres`
to move it up (positive) or down (negative), then restart the game. Width is
clamped to 0.5..10m, distance to 0.5..10m, and height to -5..5m; nonfinite
values use defaults. Panel proportions follow the game's viewport aspect.
`WorldLockedHUD=0` with `ApplyToUI=0` restores the original screen-space HUD.
The old `ApplyToUI` override is ignored while world-locked HUD is enabled.

The Steam native stereo path captures the dedicated `Id` and `IdFont` passes
once into transparent textures and projects those same pixels for both eyes.
This covers menu backgrounds, fixed-function/pretransformed UI and font draws
without relying on individual shader or blend-state identification. Scissor and
stencil clipping happen in the flat UI texture before projection. The game's UI
buffer swaps and texture/material cleanup run once, after the second eye; running
them twice consumed menu data before the right eye could draw it. Both changes
replace build .34's incomplete per-shader approach, which left menus head-locked
and visible in only one eye.

This uses the existing eye render poses and submission path. HUD size does not
depend on game FOV or `WorldUnitsPerMetre`. Hook installation checks all six
call targets against the supported Steam executable. Logs report `menu: captures`
and per-eye composite totals. The old heuristic remains only for the legacy
alternating-frame path, which does not have the dedicated native UI hooks.

Validation: `tests/build_checks.cmd` includes HUD math checks and 12 D3D9 GPU
render/readback cases covering both eyes, head turns, opaque UI, alpha edges,
separate font layers, state restoration and resource recreation. Actual title,
options and save-menu coverage was subsequently reported working by the user.

## Separate compositor process (build 20261002.33)

`ExternalCompositor=1` selects the new companion path. The proxy launches
`BayonettaVRHost.exe` beside the game; only that helper initializes OpenVR.
The game reads tracking from shared memory and renders a native stereo pair.
It publishes both completed GPU images together with their captured head poses,
tracking sample and revision. The helper copies the newest complete pair into
private textures and submits it with those exact poses. Retained images retain
their original poses. Three ownership-controlled slots bound the queue; when all
are occupied the producer drops a frame. GPU completion fences protect both
handoffs, and resolution/device changes recreate the relevant shared textures.
The game still pays for stereo rendering and GPU copies; this does not remove
GPU contention or prove a latency improvement.

Full native 6DOF starts automatically. **F8** recenters the game. **F5** displays
the helper's own tracked grid; press again to return to the game. **F7** toggles
fixed/native tracking. Legacy F2/F3/F4/F6 diagnostics are unnecessary in this mode.
`NativeStereo=1` is required. `ExternalCompositor=0` restores the previous path
and the legacy settings described below. The helper exits when the game exits.
A helper failure stops VR capture while leaving the desktop game available.

Build `BayonettaVR.sln` (Release/Win32), which builds both the DLL and helper,
or build `BayonettaVR.vcxproj` with its project reference. Copy `d3d9.dll`,
`BayonettaVRHost.exe`, `openvr_api.dll` (32-bit), and the INI beside Bayonetta.exe.
Helper logs are `BayonettaVRHost-<gamePID>.log`; the game log remains
`BayonettaVR.log`. `tests/build_checks.cmd` runs the existing math/render checks;
`tests/build_companion_checks.cmd` builds and runs a real two-process GPU transport
check without needing a headset. It checks every pixel against its eye/pose tag,
resizing, device recreation, queue backpressure, deliberate corruption detection,
shutdown, restart and parent-death cleanup.

Status: transport and regression checks pass; the helper also initialized SteamVR
and received valid tracking in a direct runtime smoke check. Automated game-menu
navigation was unavailable (Computer Use native pipe missing). This architecture is motivated by the user's smooth
standalone result and bad in-process grid results; the root cause is still unproven,
and headset smoothness with game images still requires validation.

## Legacy in-process behavior (`ExternalCompositor=0`)

- Loads automatically as a local `d3d9.dll` proxy and forwards the game's D3D9
  imports to the Windows system DLL.
- Tracks the HMD through OpenVR. By default, native rendering follows each
  eye's position while SteamVR handles rotation, as in the smooth fixed test.
  Eye positions include their movement around the head when turning. The
  default render direction stays fixed; experimental automatic turning is opt-in.
- Replaces the game's symmetric projection with each headset eye's asymmetric
  projection.
- Draws both native eyes per simulation frame, copies the D3D9 images into
  shared D3D11 textures, and submits the pair with their actual render poses.
  At 60 game fps, each eye receives 60 updates/s. Head rotation is reprojected
  by the compositor at the headset refresh rate. `NativeStereo=0` selects the
  older alternating-eye fallback.
- Keeps the normal game image on the desktop by default. Set
  `MirrorSideBySide=1` to inspect both retained eyes. Press **F8** to recenter,
  **F9** to cycle stereo separation, **F10** to swap the eyes, and **F11** to
  cycle the projection-center diagnostic. **F7** toggles positional tracking
  and the fixed smooth baseline. **F12** saves the exact submitted eye
  buffers with the active pose mode in each filename for diagnosis.
  **F6** toggles the `.27` ordinary-submit full-6DOF comparison versus the
  smooth fixed render direction, keeping positional tracking active.
  **F5** toggles a diagnostic cyan grid with a yellow center cross, rendered
  independently of the game into the submitted images.

This is an initial bring-up build, not a finished comfort mod. Camera culling,
effects that use unusual position shaders, HUD depth, and cutscenes still need
game-specific work. The Xbox 360 PDB establishes the names and layouts of the
original `cCamera`, `Hw::cCameraBase`, and view-projection systems, but its PPC
addresses cannot be used directly in the Steam x86 executable.

## Compositor rotation and positional tracking (build 20260920.30)

The default `CompositorRotation=1`, `PositionalTracking=1` combines native
positional parallax with the fixed baseline's compositor rotation. It renders
from a stable orientation at each tracked eye center and attaches that
same synthetic pose to the submitted eye image. Both eyes still share one
tracking sample and use the existing two render passes and texture bridge.

Build `.24` positional tracking was confirmed smooth by the user. Build `.25`
adds automatic render-direction updates after `CompositorTurnDegrees=12`
degrees of rotation, supporting continued turns in yaw, pitch, and roll.
The recenter origin stays fixed; both eyes change direction together before
rendering, with captured pose metadata and a compositor discontinuity flag
for the handoff. Ordinary frames keep the `.24` compositor rotation path.

Headset testing confirmed that `.25` direction handoffs cause flicker/jumps,
while F6 off followed by F8 is smooth during small turns and sideways leaning.
Builds `.26` and `.27` therefore default to `AutomaticTurning=0`. The `.26`
independent grid also jumped with stepped turning enabled, and the user
confirmed Virtual Desktop SSW was already disabled. The fault is not confined
to the game's shaders/effects. This does NOT identify a driver bug or establish
that the mod's shared camera/submission path is correct.

Build `.27` changes the F6 comparison: `DirectPoseTurning=1` renders native
full 6DOF from the latest WaitGetPoses head pose and uses `Submit_Default`
without custom pose metadata or discontinuity flags for that captured sample.
There are no 12-degree anchor updates in this mode. Captured pose stamps
prevent mode changes at Present, stale alternating eyes, or failed waits from
assigning an implicit pose to the wrong image. Other images retain the explicit
pose path. This is a diagnostic retest: earlier native tracking juddered; the
independent grid now isolates it from the game-specific rendering path.
It is NOT a confirmed smooth full-turn fix. No runtime/Virtual Desktop settings
are changed. `DirectPoseTurning=0` restores the `.25/.26` stepped comparison.
Headset result for `.27`: the jumps disappeared, but native rotation produced
severe judder in both the independent grid and game. The log confirmed ordinary
submission, matching captured sample IDs for both eyes, no anchor updates, and
roughly 59–60 game fps. These local checks do not establish final display-pose
alignment or explain the remaining judder.
Game-side culling can still omit geometry outside its original camera, and
texture edges can appear between direction updates or during fast motion.
Translation and animation still update at the game frame rate; there is no
depth-based positional reprojection. F8 explicitly recenters the world origin.
F6 off restores `.24` fixed-direction tracking. `CompositorTurnDegrees` applies
only to the legacy `DirectPoseTurning=0` test.
`CompositorRotation=0` retains the older native rotation path for diagnosis.
The log records the active tracking mode, compositor timing, anchor update
count, and angular distance from the current render direction. Named shadow
projections are excluded from head tracking so the light camera stays native.

### Early submission and independent reference-grid test (.28)

The standalone grid test was smooth in all three phases, despite measured
rates of 68.3, 46.0 and 64.3 fps. Therefore a 60-fps render rate alone does not
explain the mod's severe rotational judder. The live runtime projection also
matches the mod's X/Y projection coefficients.

Build `.28` tests one timing difference: `NativeEarlySubmit=1` submits and hands
off immediately after the native stereo pair is captured, rather than waiting
for desktop Present. It applies only to the full-native F6 comparison. The
stable startup mode, camera math, two scene passes, texture resolution, and
runtime settings are unchanged. A one-shot gate suppresses a second submission
and handoff at desktop Present, including mode changes after capture. F4 toggles
early submission versus the old `.27` timing without restarting. This is not
yet a headset-verified judder fix.

New `vr pipeline` and `vr desktop` log lines measure pose wait time, stereo-pair
capture time, delay before submission work, GPU transfer/grid time, Submit time,
pose age at Submit, desktop Present time, and pose-to-handoff time. They describe
the local pipeline, not final headset latency.

1. Enter gameplay, look forward and press F8. Leave F7 alone.
2. Press F5 to show the cyan grid/yellow cross. Try a small head turn; the
   reference should stay fixed in space. First activation may pause for shader
   compilation, so do not judge that single frame.
3. Press F6 once to enable the ordinary-submit full-6DOF comparison. Turn slowly
   about 20–30 degrees. Report whether the grid and game are steady, both
   judder/jump, or only the game is unstable. Do not change refresh rate or SSW.
4. F6 again disables automatic turning; F5 hides the grid. F8 recenters.
5. While F6 full-native mode is active, F4 switches between early and old timing.
   Compare small head turns for about ten seconds in each setting. Leave F7 alone.

### Exact captured-pose submission test (.29)

Build `.29` keeps the user-confirmed smooth startup path unchanged. With F6
enabled, it still renders native full 6DOF from the latest `WaitGetPoses`
sample, but `NativeExplicitPose=1` now attaches that exact captured pose to
each submitted eye through `Submit_TextureWithPose`. This isolates ordinary
implicit pose association from the render math and the independent grid.
Early submission remains enabled, and F4 continues to compare early versus
desktop-Present timing. This is a headset diagnostic, not a confirmed fix.

### Audited ordering and clean grid isolation (.30)

Build `.30` defaults `NativeEarlySubmit=0`, combining native full-6DOF
rendering and exact captured-pose submission with the smooth baseline's order:
submit the pair, Present the desktop companion, then call
`PostPresentHandoff`. This exact combination was missing from `.27` through
`.29`.

F5 now submits a clean 1024x1024 D3D11 grid for each eye. These textures are
cleared and rendered directly like the standalone probe; the D3D9 shared
surface and game image are not copied into them. This makes F5 an isolation
test for the bridge rather than merely an overlay. The game continues running
on the desktop while the HMD shows only the grid. F5 again restores the game.

The grid is a virtual plane three metres ahead of the recenter pose, drawn
without depth occlusion. It is not attached to Bayonetta's moving camera, so
keep the controller camera still during this test. It uses a private D3D11
shader after the game image is copied, bypassing game shaders, culling and
post-processing. Its projection and render pose are captured with each eye,
before key handling can change tracking state. This separates game-rendering
errors from the shared tracking/projection/submission path; it does not prove
which individual component in either path is faulty.

F5 off adds no grid draw calls. F5 on draws 32 lines per eye and is diagnostic,
not a performance benchmark. F12 screenshots retain the raw game eye buffers
and do not contain this post-copy grid. The test suite includes D3D11 WARP
render/readback checks for grid placement, both eyes, turns, and resource reset.
The log's `vr submit audit` records pose sample stamps, actual submit flags,
and ordinary/explicit submission counts. In `.27` F6 mode, normal paired frames
should show matching samples and `flags=0x0/0x0`. This verifies our submission
selection, not the final headset display. Submission-policy tests also cover
key transitions after capture, failed waits, and stale samples.

### Standalone display-rate comparison

`tests\build_standalone_probe.cmd` builds
`build\test\standalone_tracking_probe.exe`. This is a separate OpenVR Scene
application, not another deployed mod version. Run it only with Bayonetta
closed and the headset ready; it temporarily takes VR scene focus and refuses
to start if Bayonetta is running. Escape exits early.

The 24-second test draws only the reference grid using the runtime's RH
projection and calibrated eye/head matrices directly. It does not use the
mod's camera conversion, game projection, D3D9 bridge, or game timing loop.
It retains the independent D3D11 grid renderer and ordinary texture submission.
Use small head turns throughout; the grid is a front-facing plane, not a 360°
environment. The first 8 seconds use a blue background and the display-rate
target (120 Hz on the tested headset); the next 8 use green and a half-rate
target; the final 8 return to blue. Actual rates, maximum pose intervals and
submission errors are printed, so targets must not be mistaken for measurements.
No runtime, SSW, refresh-rate, or installed-mod settings are changed. Shader
compilation happens before the timed phases. The user reported no head-turn
judder in any phase. Measured rates were 68.3 / 46.0 / 64.3 fps, with 1,429
submitted pairs and no submission failures; this was not a clean 120/60 test.

Validation: `tests\build_checks.cmd` builds and runs camera math tests over
both eyes, combined rotations, translations, different scene depths, and
4,323 direction-following samples through +/-720 degrees on each rotation
axis. Shader tests include captured scene, skinned, and shadow shader assembly.
`build\test\projection_probe.exe` compares the mod's projection against the
running headset using a background OpenVR connection without submitting frames.
Passing these checks does not establish headset comfort; that needs play testing.

## Independent in-process grid (build 20261002.31)

Press **F3** during gameplay to show a cyan grid on a **purple** background.
Look forward when enabling it, then make small head turns for about ten seconds.
**F8** recenters this grid; **F3** again returns to the previous game VR mode.
F4 through F7 and F9 through F12 are inactive while the worker owns submission.
Startup behavior and the installed INI settings are unchanged.

This diagnostic runs the successful standalone probe's direct OpenVR RH camera
math, 1024x1024 D3D11 textures, completion fence, ordinary submission and handoff
on a dedicated thread with its own device/context. It shares the existing OpenVR
session; it does not initialize a second Scene application. The game continues
both scene render passes and desktop presentation with its last tracking pose,
but skips eye capture and all game-thread compositor calls during the test.
Unlike F5, the F3 grid does not inherit a pose or submission schedule from the game.

Ownership changes at the outer desktop Present boundary. On stop or failure,
the game waits for actual worker exit, discards the transition frame and resumes
with a fresh native stereo pair. Resetting D3D9 does not release worker resources.
No blocking thread join is performed in DllMain. The ownership tests exercise
25 stop/restart and early-failure cycles, plus explicit shutdown.

The log reports `independent grid: RUNNING`, two-second frame-rate summaries,
maximum pose-to-submit delay, worst pose interval, submit failures, decoded
prediction/throttle counts and worker exit. These are local pipeline measurements,
not headset display latency. A smooth F3 grid implicates differences in the
current game-coupled tracking/submission path; a juddering F3 grid requires
investigating the in-process environment and GPU load before camera changes.
Headset result on 2026-10-02: the independent F3 grid still lagged severely.
Its ten two-second reports usually measured 108-117 fps (one interval 82 fps),
with zero Submit errors. Most intervals had maximum pose-to-submit times of
1-4 ms, but there were spikes to 61 and 345 ms and pose gaps up to 347 ms.
Prediction remained 2-3 additional frames, with zero reported throttling in
these samples. This rejects game-thread scheduling as the sole explanation;
it does not establish that the runtime or GPU is the root cause.
The concurrent SteamVR compositor log contains driver texture-handoff timeouts
and ReleaseSync errors, also present before the game launched. Those errors
need a current standalone comparison; the older smooth standalone result
must not be treated as a same-session control. Evidence is preserved under
`.diagnostics/independent31-result-20261002`. No full-6DOF fix is confirmed.

## Same-session game-load comparison (build 20261002.32)

The unchanged standalone executable was retested on October 2 in the same
SteamVR session and the user reported smooth tracking. It measured 67.0 / 44.6 /
63.6 fps and 1,402 submitted pairs without failures. Its dark green phase did
run according to the log, but was not visually obvious. Compositor handoff
warnings also occurred around this test, so they do not by themselves explain
why the in-game grid judders.

In the F3 independent grid, **hold F2** to park Bayonetta's render/game thread
at the end of its current desktop Present. The background becomes **bright
green** while the VR worker continues the same loop. Release F2 to resume the
game and restore purple. Parking automatically ends after four seconds; release
and press F2 again to repeat. F3 still exits. Judge small head turns during the
middle of the green interval, allowing previously queued GPU work to drain.

This temporarily stops game simulation, rendering and desktop presentation;
it leaves the VR session, worker, camera matrices and submission policy intact.
It does not identify rendering versus Present versus another game-thread effect
individually. A smooth green interval implicates ongoing game-thread activity.
Lag in both colors leaves process/session initialization and other differences
from the standalone unresolved. Logs mark both transitions and label the
worker's timing summaries with `parked=0/1`.

Headset result: the user reported severe lag in every color, including green.
The log confirms two parked reports at 111 and 118 fps with maximum local
pose-to-submit times of 1.42 and 0.85 ms, no Submit failures and three extra
predicted frames. This rejects ongoing render-thread work as a sufficient
explanation; other game threads and process/session state were still present.
Evidence is in `.diagnostics/parked32-result-20261002`.

Subsequent local startup probes compared normal initialization, single-precision
FPU initialization, an early WaitGetPoses call, and ForceInterleavedReprojectionOn(false).
Their timing differences did not establish a causal explanation or a comfort fix.
A rebuild of the original standalone has byte-identical .text, .data, .fptable
and .reloc sections to the user-confirmed smooth executable. A final timing
comparison became unusable (roughly 9 fps followed by OpenVR initialization
error 117); those measurements are not evidence of a rendering regression.
The original executable and installed .32 DLL remain unchanged. No fix is claimed.

## Build

Open `BayonettaVR.sln`, select `Release | Win32`, or run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' BayonettaVR.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32
```

The build output is `build\Win32\Release\d3d9.dll`.

## Install and test

1. Exit Bayonetta.
2. Copy the built `d3d9.dll`, `BayonettaVR.ini`, and the **32-bit**
   `openvr_api.dll` into the folder containing `Bayonetta.exe`.
3. Start SteamVR, then launch Bayonetta normally.
4. Inspect `BayonettaVR.log` in the game folder. A successful run reports the
   D3D9 hooks, detected shader matrix registers, shared eye textures, and
   compositor submit results.

Remove or rename the local `d3d9.dll` to uninstall. Existing game files are not
patched.

## Tuning and troubleshooting

`WorldUnitsPerMetre` controls tracked head translation and `StereoScale`
independently controls IPD. `GameVerticalFovDegrees` controls the inverse
projection used to introduce the tracked eye camera. If the eyes are reversed,
set `SwapEyes=1` or press F10. If automatic shader analysis fails, the log will
make that evident; `MatrixRegisterOverride` is available for a confirmed
constant register during bring-up.
