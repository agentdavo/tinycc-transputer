# Night Relay

Night Relay is a one-table pinball game for TransputerOS. The player runs a mountain observatory during a night storm, bouncing a silver ball through three stations to assemble and transmit dispatches. A successful transmission lights the entire board and releases a second ball.

## The cabinet

The 800×600 display frames a narrow, upright 320×552 playfield at screen position (240, 24). The left panel shows score, ball count and the dispatch record. The right panel shows the next shot, launch power and tilt meter. A stepped mountain silhouette, engraved brass rails and a field of tiny blue stars give the cabinet a strong outline at emulator scale. The table remains top-down so that ball paths agree with the art.

Use deep indigo (`#111A31`) for the lacquer, muted blue (`#32486B`) for inactive guides, brass (`#D8A460`) for rails, electric cyan (`#55DEEF`) for an available shot and amber (`#FFB344`) for a completed one. Available shots get a pulsing arrow. Completed ones get a solid diamond. The ball gets a white leading highlight and a short violet shadow. Bumpers throw a brief halo. When a dispatch transmits, a cyan wave climbs the rails before the second ball enters. The strong highlights stay on the moving ball, attainable shots and near-flipper collision surfaces.

The table blockout uses playfield-local coordinates.

| Feature | Placement | Shot and return |
| --- | --- | --- |
| West Orbit | Entry (53, 230), outer curve across y=40 | A left-side orbit carries the ball behind the bumpers and returns through the right inlane. |
| Star Array | Bumpers around (135, 120), (174, 96), (210, 134) | Three distinct bumper hits light the station. The cluster feeds both flippers. |
| East Switchback | Entry (245, 266), bend near (267, 158) | An angled right shot rises into an enclosed ramp and returns to the left inlane. |
| Transmission Dish | Centre scoop at (161, 206) | A lit centre shot captures the ball briefly and feeds it to the left flipper. |
| Lower field | Flipper pivots (105, 487) and (215, 487) | The inlanes feed the flippers. The outlanes and central gap feed the drain below y=532. |
| Shooter lane | Right wall, plunger at (297, 518) | The ball loops above the Star Array. A marked launch-power band aims at the skill-shot gate. |

Use a flat collision plane with a scripted ramp ride after crossing its entry gate. Draw the ramp above the playfield with a separate ball drawing layer during the ride. Rails, gates and scoring sensors should come from the same table data used to generate the artwork. Tune the entries and rail endpoints against instrumented replays before baking the final art.

## Rules and controls

- Start with three balls. A launch within the marked power band earns a skill shot if the ball crosses the upper gate on its first pass.
- A completed West Orbit, East Switchback or Star Array earns one stamp for the current dispatch. After all three stamps, the Transmission Dish opens for ten seconds of simulation time. On timeout the stamps remain lit and the next completed station reopens the dish.
- The dish awards a dispatch, kicks the captured ball back into play and feeds a second ball from the shooter lane. During multiball, a completed orbit and a completed ramp each light their jackpot once. The dish collects the lit jackpots. When one ball drains, play returns to one ball and starts a fresh dispatch without charging a ball for the multiball drain.
- A short ball save covers each normal launch. A clean inlane feed gives the player time to cradle the ball. Nudge moves the ball sideways and fills the tilt meter. Tilt disables flippers and scoring until the drain.
- Hold the left and right mouse buttons for the corresponding flippers. The mouse UART carries button state and supports both buttons together. Keyboard `Z` and `X` trigger fixed-duration flipper pulses, with `C` firing both. The T4 keyboard sends key presses without release events. Space starts the moving plunger gauge and a second press launches. Use `A` and `D` to nudge, `P` to pause, and Escape to return to the shell or desktop.

The attract mode uses a recorded input sequence, so it can teach the three shots and serve as a reproducible demo. Store session best scores in memory for the first release. The table communicates every event visually. Add optional sound only after the target has a usable audio backend.

## Engine and display

- Keep table data, simulation and rendering separate. Run a deterministic 120 Hz simulation with signed 32-bit positions in units of 1/256 pixel and cap the speed at 10 pixels per tick. Use four substeps, a six-pixel ball radius, and swept contact tests for thin rails and moving flippers. Precompute rail normals and flipper poses, use a small spatial grid for candidate colliders and check the two balls against each other during multiball. Specify rounding for negative fixed-point values and bound intermediate products before narrowing.
- Validate the port's LDTIMER-backed `gettimeofday` through both syscall routes and clamp elapsed deltas before feeding the tick accumulator. Simulation advances in whole ticks with a bounded catch-up after a pause. The current `clock_gettime` handler returns zero timestamps and the sleep handler returns immediately. Attract mode and test replays advance from recorded tick input independently of the clock.
- Start with exclusive framebuffer presentation using the existing address profiles in `fbwm_theme.h` and the XRGB8888/FLIP contract in `i_video_tp.c`. Render the static cabinet once. Restore dirty playfield rectangles from cached indexed rows, draw overlapping sprites in order, and issue one FLIP per complete frame. Use palette-indexed sprites and pre-rendered flipper poses, and keep glow animation within local damage boxes. A 320×552 indexed playfield cache is about 173 KiB. Reuse the existing framebuffer carveout and target less than 1 MiB of additional resident memory, including the bFLT image and workspace.
- Target 60 presented frames per second, with a 30 Hz redraw option that keeps the 120 Hz simulation. Profile update, restore and present separately on the 32 MiB emulator. Consider bulk copies only after measuring a benefit on aligned, non-overlapping rows. Defer windowed play until the exclusive version passes. The existing Doom shared-memory guest path provides the model for publishing a later, smaller client surface.

## Package and release

Put the implementation under `transputer/games/night-relay/`. Follow the Doom build for the startup object, CRT bridge, uClibc, target runtime and syscall stub. Build with TinyCC and strict unresolved-call checks, select the framebuffer profile for the target, then convert the executable with `transputer-elf2flt`.

Add a `night-relay` target to `transputer/packages/build-packages.sh` using `make_tpz`. Stage an executable `usr/games/night-relay` and the original art and controls documentation under `usr/share/doc/night-relay/`. Package any runtime asset bank under `usr/share/night-relay/`. Produce `night-relay-0.1.0.tpz` with a manifest as the first archive member and relative paths. Keep the archive optional, with the package builder recording the selected license and measured installed size.

Install via `tpkg install /sd/night-relay-0.1.0.tpz` and run `/usr/games/night-relay` from `tpsh`. Add an fbwm launcher after shell launch is stable. Follow the existing synchronous spawn pattern and recompose the desktop when the game exits.

### Release gates

1. Run deterministic host and target replays for launch and drain, all three shots, dish capture, tilt, and two-ball contacts. Compare tick-state hashes and scoring events. Check that a held bumper or sensor cannot award repeated points without a new contact.
2. Compare complete framebuffer captures at the attract screen, flipper-up, dispatch, multiball and game over. Test live mouse holds, simultaneous flippers, keyboard taps and pause on the visible emulator.
3. Install, run and remove the package on the 32 MiB guest. Run after CoreMark, Doom and native TinyCC, then exit back to fbwm and compare the restored desktop to its reference. Exercise both trap and legacy C-call builds. Record peak additional memory, simulation and redraw timing, and package size.

## Version 0.1.0 validation

The playable build uses the Transputer timer and the guest's framebuffer
and UART devices. Its desktop icon appears when the executable is present.
The optional package is 21,758 bytes. See the
[game README](../games/night-relay/README.md) for builds, controls and SD
staging on guests with minimal BusyBox.

The 32 MiB T4 test staged the exact package payload on SD and ran CoreMark,
Doom and native TinyCC before starting fbwm. The scripted physics test
matched all 480,000 RGB pixels of the host multiball reference. Live
input launched a ball, raised both flippers and exited with Escape.
The restored desktop matched the pre-launch frame outside the taskbar
clock. Host testing also checked that all 81 launch settings feed and
drain.
