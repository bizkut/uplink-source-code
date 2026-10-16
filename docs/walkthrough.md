# UplinkOS GOLD Reconstruction Walkthrough

## Reverse-engineering path

1. Fingerprinted `UplinkOS.exe` as a 4,712,448-byte PE32/i386 image with SHA-256 `4461a546b74f19ea3874a72723c4c743731c5b84de09c209950e85684e1a7fd8`.
2. Confirmed conventional sections/entropy, Visual C++ RTTI, source paths, Allegro 5.2.1, TinyXML2, and libpng 1.6.17. No packing layer was found.
3. Used the supplied 801k-line final pseudocode, final XML/assets, and IDA xrefs/disassembly to map HD classes, save revisions, model fields, and gameplay guards.
4. Compared each recovered behavior with the released source. Historical HD source was treated as an early reference, never as final authority.
5. Replaced the incomplete HD stubs with an owned XML UI tree and migrated complete, executable slices rather than visual-only callbacks.

## Principal recovered flows

### First run

`HD_Intro_Create` (`0x472f30`) constructs eight `IntroScreens.xml` steps. The recovered callbacks validate credentials, call the shared new-agent bootstrap (`0x4686a0`), select physical location `(215,215)`, display gateway progress, then complete setup through `0x4711a0`. The Linux port follows that state machine and retains the old Script30 path only when HD initialization fails.

### Login/options

`HD_MainMenu_Create` (`0x475d20`) loads the last user/date, marks the password field secret, opens final MMOptions, and submits if the password is nonempty and the username matches an existing profile. Profile selection uses the target sentinel `PASSWORD70RZ`. Shutdown and profile deletion use HD confirmation dialogs.

### HUD utilities

Top/task bars read live world/player/account/task/message state. Notes use one independent window per note and final XML persistence. Task Manager V2 keys rows by PID, eliminating equal-count replacement/phantom-row failures; priority/pin/close actions resolve the current scheduler index at invocation time.

### SAV67

The target's version gates established four additions: read flags, person gender, message attachments, and mission target labels. The Linux reader/writer supports those exact gates and exercises them through a create-save-restart-load scenario.

## Reproducing the verified run

```bash
# After building the prerequisites listed in BUILDING.linux:
make -C uplink/src -f Makefile.linux -j4
install -m 755 uplink/src/uplink.full uplink/bin/uplink
cd uplink/bin
./uplink
```

Observed smoke scenario:

1. Fresh `$HOME`: HD server prompt appeared.
2. Navigated About, registration package, and Create User.
3. Empty username produced the final HD error text.
4. Created `RoundTrip`, selected a gateway city, and completed progress.
5. Observed the HD HUD and immediate encrypted `RoundTrip.usr/.tmp` files.
6. Quit/restarted, selected the recent profile, and loaded back to the gateway.
7. Exercised options, notes persistence, Task Manager empty state, logout, and shutdown confirmation.

## Boundary

The result is a maintainable Linux source reconstruction, not a reproduction of
the Windows machine code. Every target-defined HD surface has a source
controller. Screens for which the target has no HD class/layout—notably Code
Card and Nearest Gateway—intentionally retain the original Eclipse renderer.
See `MOD_FIXES.md` for the implementation and verification ledger.
