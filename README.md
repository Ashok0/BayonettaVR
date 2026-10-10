# BayonettaVR

Experimental native-stereo 6DOF OpenVR mod for the 32-bit Steam release of
Bayonetta (executable timestamp `0x591AD330`).

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
