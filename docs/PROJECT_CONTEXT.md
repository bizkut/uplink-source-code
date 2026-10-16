# Project Context

## Goal

Reconstruct UplinkOS GOLD v1.04 behavior from the modified binary and port it onto the released Linux Uplink source.

The reference executable is `uplink/mod/UplinkOS.exe`, SHA-256
`4461a546b74f19ea3874a72723c4c743731c5b84de09c209950e85684e1a7fd8`.
The Linux result is a source-level behavioral reconstruction, not a byte-identical PE rebuild.

## Evidence order

1. `uplink/mod/UplinkOS.exe.c`: final target pseudocode.
2. `uplink/mod/UplinkOS.i64` / live IDA xrefs and disassembly: addresses, bounds, call/data references.
3. `uplink/mod/UplinkOS_ReadMe.txt`: claimed feature/change inventory, not proof of implementation.
4. `uplink/mod/uplinkHD/`: final layouts/assets/default hotkeys/name data.
5. Released source and the historical CptMcBacon branch: reusable intent only; final binary wins on conflicts.

Do not infer behavior from a changelog entry when the final code is available. Record unresolved target behavior rather than inventing it.

## Source map

| Path | Responsibility |
|---|---|
| `uplink/src/app/` | process, options, paths, serialization helpers, graphics loop |
| `uplink/src/game/` | game lifecycle and one-shot scripts |
| `uplink/src/world/` | persistent model, generation, scheduler, missions |
| `uplink/src/interface/` | original Eclipse local/remote screens and hacking tasks |
| `uplink/src/hd_ui/` | reconstructed Allegro/XML UI runtime and migrated GOLD screens |
| `lib/gucci/` | Allegro event/display bridge |
| `lib/vanbakel/` | task scheduler and priorities |
| `uplink/bin/uplinkHD/` | runtime HD assets |
| `uplink/mod/` | reverse-engineering evidence |

## Current architecture

The original OpenGL/Eclipse renderer remains the gameplay model and callback
backend. Allegro 5 owns the display/event queue. The HD manager borrows that
display, observes input before Eclipse, draws after Eclipse, and never flips
independently. Migrated trees are exclusively owned beneath one scaled root.

Every screen for which the target defines a final `HD_*` controller/layout now
has a source controller: onboarding and main menu, HUD/local windows, remote
computer services, hacking applications, map/LAN, and ending screens. Eclipse
continues to render only target screens without an HD counterpart, notably Code
Card and Nearest Gateway. Save format is SAV67. Linux user files live in
`$HOME/.uplink/`; runtime assets resolve from `app->path`, so run from
`uplink/bin`.

## Build

Use `BUILDING.linux`. The final executable is produced at `uplink/src/uplink.full`, then deployed to `uplink/bin/uplink`. `Makefile.linux` must keep `USE_ALLEGRO5` defined or HD initialization is compiled out.

## Verification standard

For UI changes, launch the binary and interact with the actual surface. For save/model changes, create a profile, stop/restart the process, and load it. A successful compile alone is not completion. Keep `docs/MOD_FIXES.md` aligned with observed verification and residual gaps.
