# BayonettaVR

Experimental native-stereo 6DOF OpenVR mod for the 32-bit Steam release of
Bayonetta (executable timestamp `0x591AD330`).

## What this first build does

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
  **F6** toggles automatic turning versus the `.24` fixed render direction,
  keeping positional tracking active for a direct comparison.
  **F5** toggles a diagnostic cyan grid with a yellow center cross, rendered
  independently of the game into the submitted images.

This is an initial bring-up build, not a finished comfort mod. Camera culling,
effects that use unusual position shaders, HUD depth, and cutscenes still need
game-specific work. The Xbox 360 PDB establishes the names and layouts of the
original `cCamera`, `Hw::cCameraBase`, and view-projection systems, but its PPC
addresses cannot be used directly in the Steam x86 executable.

## Compositor rotation and positional tracking (build 20260915.26)

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
Build `.26` therefore defaults to `AutomaticTurning=0`. This is a diagnostic
build, NOT a fix for unrestricted head turning. F6 re-enables the same `.25`
turning path for comparison; its threshold has not been disguised by filtering.
Game-side culling can still omit geometry outside its original camera, and
texture edges can appear between direction updates or during fast motion.
Translation and animation still update at the game frame rate; there is no
depth-based positional reprojection. F8 explicitly recenters the world origin.
F6 or `CompositorTurnDegrees=0` restores `.24` fixed-direction tracking.
`CompositorRotation=0` retains the older native rotation path for diagnosis.
The log records the active tracking mode, compositor timing, anchor update
count, and angular distance from the current render direction. Named shadow
projections are excluded from head tracking so the light camera stays native.

### Independent reference-grid test (.26)

1. Enter gameplay, look forward and press F8. Leave F7 alone.
2. Press F5 to show the cyan grid/yellow cross. Try a small head turn; the
   reference should stay fixed in space. First activation may pause for shader
   compilation, so do not judge that single frame.
3. Press F6 once to enable experimental turning. Turn slowly about 20–30 degrees.
   Report whether **the grid itself jumps**, or **only the game jumps behind it**.
4. F6 again disables automatic turning; F5 hides the grid. F8 recenters.

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

Validation: `tests\build_checks.cmd` builds and runs camera math tests over
both eyes, combined rotations, translations, different scene depths, and
4,323 direction-following samples through +/-720 degrees on each rotation
axis. Shader tests include captured scene, skinned, and shadow shader assembly.
`build\test\projection_probe.exe` compares the mod's projection against the
running headset using a background OpenVR connection without submitting frames.
Passing these checks does not establish headset comfort; that needs play testing.

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
