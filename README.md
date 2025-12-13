# Uplink Source Code - Linux Port with UplinkOS Mod Fixes

A port of the Uplink game source code to modern Linux systems, incorporating bug fixes from the UplinkOS mod.

## Project Status

| Component | Status |
|-----------|--------|
| Source Compilation | ✅ 199 files compiled |
| Library Build | ✅ 10/10 libraries |
| Linking | ❌ FTGL compatibility issue |
| Mod Fixes | 🔄 2/16 integrated |

## Building

### Prerequisites
```bash
sudo apt install build-essential libsdl1.2-dev libsdl-mixer1.2-dev \
    libftgl-dev libfreetype6-dev libtiff-dev libgl1-mesa-dev libglu1-mesa-dev
```

### Build Steps
```bash
# Build libraries
cd lib && make

# Build contrib libraries
cd ../contrib/tcp4u.331/src && make -f ../Unix/Makefile
cd ../../irclib && make
cd ../unrar && make

# Build main executable
cd ../../uplink/src && make -f Makefile.linux
```

## UplinkOS Mod Fixes Integrated

Changes from the UplinkOS mod that have been ported:

- **Bank Transfer Validation** - All fields must be filled before transfer
- **Stock Account Validation** - Username/password checks for stock accounts

See `docs/MOD_FIXES.md` for the full list of fixes to be integrated.

## Directory Structure

```
uplink-source-code/
├── uplink/src/      # Main game source code
├── lib/             # Internal libraries (gucci, eclipse, etc.)
├── contrib/         # Third-party dependencies
└── .agent/          # AI agent workflows
```

## License

Original Uplink source code is © Introversion Software.
