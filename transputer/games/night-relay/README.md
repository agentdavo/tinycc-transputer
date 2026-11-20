# Night Relay

Play an original pinball table in a storm-lit summit observatory. Complete the West Orbit, light all three Star Array bumpers, and shoot the East Ramp to open the Transmission Dish. A shot into the open dish releases a second ball. Complete the orbit and ramp during multiball to light the jackpots, then collect them at the dish.

| Control | Action |
| --- | --- |
| Space | Start the launch gauge. Press again to launch. Aim for the brass band for a skill shot. |
| Mouse left and right | Hold the corresponding flipper. |
| Z and X | Tap the left and right flippers. Press C to tap both. |
| A and D | Nudge left or right. Repeated nudges can tilt the table. |
| P | Pause or resume. |
| Escape | Quit to the shell or desktop. |

Install the archive on the guest with `tpkg install /sd/night-relay-0.1.0.tpz`, then launch Night Relay from the desktop or run `/usr/games/night-relay` in the shell. Use `tpkg remove night-relay` to uninstall. With an older package manager, you can unpack the package on the host into the mounted SD filesystem. The desktop then finds the game at `/sd/usr/games/night-relay`.

```sh
tar xzf night-relay-0.1.0.tpz -C /path/to/mounted-sd
```

The game generates its cabinet art at launch.

## Build

Run from the TinyCC checkout with the Transputer tools and uClibc built.

```sh
SPINE=1 TP_USE_T9K_SYSCALL=1 bash transputer/games/night-relay/build.sh
bash transputer/packages/build-packages.sh night-relay
```

Use the framebuffer profile for the target kernel. Set `SPINE=0` for the original T4 framebuffer addresses. The build accepts `TCC`, `ELF2FLT`, `UCLIBC` and `LIBTCC1` overrides and an optional output path. If the bFLT lives elsewhere, set `NIGHT_RELAY_FLT` when building the package. The optional archive is written to `transputer/packages/output/`.

## Verify

Run `/usr/games/night-relay --verify` to exercise launch, all three relay stations, dish capture, multiball, tilt and drain. It draws a fixed multiball frame and prints its framebuffer checksum over UART before returning. Generate the host reference with the same game and renderer.

```sh
cd transputer/games/night-relay
cc -std=c99 -O2 -DNIGHT_RELAY_HOST main.c relay.c cabinet.c -o /tmp/night-relay
/tmp/night-relay /tmp/night-relay.ppm
```
