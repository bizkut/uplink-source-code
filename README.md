# UplinkOS GOLD Linux Reconstruction
[![Linux build](https://github.com/bizkut/uplink-source-code/actions/workflows/linux-build.yml/badge.svg)](https://github.com/bizkut/uplink-source-code/actions/workflows/linux-build.yml)

Source-level Linux reconstruction of the modified Windows binary `UplinkOS.exe` (UplinkOS GOLD v1.04) on top of the released Uplink source tree.

Reference target:

- SHA-256: `4461a546b74f19ea3874a72723c4c743731c5b84de09c209950e85684e1a7fd8`
- PE32/i386, Visual C++ linker 12.0, 4,712,448 bytes
- PDB identity: `e7c379bc-d2ad-438e-b1a6-8be8691604f1`, age 1
- Final decompiler output: `uplink/mod/UplinkOS.exe.c`
- Final assets and layouts: `uplink/bin/uplinkHD/`

This produces a native Linux build, not a byte-identical Windows executable. Recovered save/gameplay behavior and the implemented HD surfaces are documented in `docs/MOD_FIXES.md`.

## Build and run

See `BUILDING.linux` for dependencies and the complete build sequence.

```bash
make -C contrib/FTGL-2.1.2 -j4
make -C contrib/tcp4u.331/src -f ../Unix/Makefile -j4
make -C contrib/irclib -j4
make -C contrib/unrar -j4
make -C lib -j4
make -C uplink/src -f Makefile.linux -j4
install -m 755 uplink/src/uplink.full uplink/bin/uplink
cd uplink/bin
./uplink
```

The executable must run from `uplink/bin`; that directory contains the data archives and `uplinkHD` assets resolved through `app->path`.

## Continuous integration

`.github/workflows/linux-build.yml` builds the complete source on Ubuntu 24.04
for pushes to `master`, pull requests, and manual runs. It publishes a
`uplinkos-gold-linux-x86_64` artifact containing both a runnable `bin/` archive
and an AppImage, with SHA-256 checksums and build documentation. Both packages
embed the required data archives, unpacked data/music, and complete `uplinkHD`
tree; neither depends on repository-relative symlinks. Windows-only DLLs,
backup files, and runtime caches are excluded.

## Key paths

| Path | Purpose |
|---|---|
| `uplink/src/hd_ui/` | Complete GOLD UI runtime: menus/HUD, local windows, remote screens, apps, map/LAN, popups, and end screens |
| `uplink/src/` | Original game plus recovered model, scheduler, catalog, and gameplay changes |
| `uplink/mod/` | Authenticated target artifacts, decompiler output, layouts, localization database |
| `uplink/bin/uplinkHD/` | Runtime layouts, fonts, graphics, sounds, names, default hotkeys |
| `packaging/appimage/` | Reproducible AppImage metadata and complete runtime-data packager |
| `docs/MOD_FIXES.md` | Evidence ledger, reconstructed behavior, verification, and limits |
| `docs/UI_MOD_ANALYSIS.md` | HD architecture, complete surface coverage, and backend boundaries |
