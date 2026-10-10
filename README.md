# BayonettaVR

Experimental native-stereo 6DOF OpenVR mod for the 32-bit Steam release of
Bayonetta (executable timestamp `0x591AD330`).

## What this first build does

- Loads automatically as a local `d3d9.dll` proxy and forwards the game's D3D9
  imports to the Windows system DLL.
- Tracks the HMD through OpenVR and applies the full relative head pose to the
  position transform found in Bayonetta's vertex shaders.
- Replaces the game's symmetric projection with each headset eye's asymmetric
  projection.
- Renders alternating native eyes, retains both D3D9 frames in shared D3D11
  textures, and submits the pair to SteamVR. This provides real geometric
  stereo without running the game simulation twice. Each eye is refreshed on
  alternating game frames, so a 60 fps game gives 30 updates/s per eye.
- Keeps the normal game image on the desktop by default. Set
  `MirrorSideBySide=1` to inspect both retained eyes. Press **F8** to recenter,
  **F9** to cycle stereo separation, **F10** to swap the eyes, and **F11** to
  cycle the projection-center diagnostic. **F12** saves the exact submitted
  eye buffers as `BayonettaVR_eye0.bmp` and `BayonettaVR_eye1.bmp` for diagnosis.

This is an initial bring-up build, not a finished comfort mod. Camera culling,
effects that use unusual position shaders, HUD depth, and cutscenes still need
game-specific work. The Xbox 360 PDB establishes the names and layouts of the
original `cCamera`, `Hw::cCameraBase`, and view-projection systems, but its PPC
addresses cannot be used directly in the Steam x86 executable.

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
