# BayonettaVR

Experimental native-stereo 6DOF OpenVR mod for the 32-bit Steam release of
Bayonetta (executable timestamp `0x591AD330`).

## What this first build does

- Loads automatically as a local `d3d9.dll` proxy and forwards the game's D3D9
  imports to the Windows system DLL.
- Tracks the HMD through OpenVR. By default, native rendering follows each
  eye's position while SteamVR handles rotation, as in the smooth fixed test.
  Eye positions include their movement around the head when turning.
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

This is an initial bring-up build, not a finished comfort mod. Camera culling,
effects that use unusual position shaders, HUD depth, and cutscenes still need
game-specific work. The Xbox 360 PDB establishes the names and layouts of the
original `cCamera`, `Hw::cCameraBase`, and view-projection systems, but its PPC
addresses cannot be used directly in the Steam x86 executable.

## Compositor rotation and positional tracking (build 20260915.24)

The default `CompositorRotation=1`, `PositionalTracking=1` combines native
positional parallax with the fixed baseline's compositor rotation. It renders
from the recentered orientation at each tracked eye center and attaches that
same synthetic pose to the submitted eye image. Both eyes still share one
tracking sample and use the existing two render passes and texture bridge.

This is an experimental limited-view 6DOF mode: large head turns can expose
unrendered edges, as in the fixed baseline. F8 recenters the rendered view;
use the game camera for larger turns. Translation and animation still update
at the game frame rate; there is no depth-based positional reprojection.
`CompositorRotation=0` retains the older native rotation path for diagnosis.
The log records the active tracking mode and compositor frame timing.

Validation: `tests\build_checks.cmd` builds and runs camera math tests over
both eyes, combined rotations, translations, and different scene depths.
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
