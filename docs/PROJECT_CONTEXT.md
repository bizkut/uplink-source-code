# Uplink Source Code - Project Context

This document provides context for AI agents and developers working on the Uplink source code.

## Project Overview

**Uplink** is a 2001 hacking simulation game by Introversion Software. This is the **official source code** released for Linux, with modifications from the **UplinkOS mod** (Windows) being integrated.

## ⚠️ CRITICAL AGENT INSTRUCTIONS

**ALWAYS REFER TO THE DECOMPILED MOD CODE (`uplink/mod/UplinkOS.exe.c`) FIRST.**

When investigating features, bugs, or logic from the UplinkOS mod:
1.  **Do not guess** implementation details.
2.  **Search** the decompiled C code using unique string literals, error messages, or constant values.
3.  **Compare** the mod's logic side-by-side with the original source.
4.  **Replicate** the exact logic used in the mod where appropriate.

The decompiled code is the **primary source of truth** for all functional changes.

## Directory Structure

```
uplink-source-code/
├── contrib/          # Third-party libraries (SDL, FTGL, etc.)
├── lib/              # Compiled library outputs
├── uplink/
│   ├── bin/          # Compiled game binary + data files
│   ├── mod/          # UplinkOS mod files (reference)
│   │   ├── UplinkOS.exe.c   # Decompiled mod (801k lines)
│   │   └── UplinkOS_ReadMe.txt  # Mod changelog (IMPORTANT!)
│   └── src/          # Game source code
│       ├── app/      # Application core
│       ├── game/     # Game logic, scriptlibrary
│       ├── interface/# UI components
│       │   ├── taskmanager/  # Hacking tools (passwordbreaker, etc.)
│       │   ├── localinterface/  # Player-side UI (LAN view, etc.)
│       │   └── remoteinterface/ # Server-side screens
│       └── world/    # Game world (computers, companies, etc.)
└── docs/             # Documentation
```

## Key Source Files

| File | Purpose |
|------|---------|
| `scriptlibrary.cpp` | Button click handlers (Script10=bank transfer, Script12=stock account) |
| `passwordbreaker.cpp` | Password cracking tool |
| `voiceanalyser.cpp` | Voice analysis tool |
| `filecopier.cpp` | File download tool |
| `logdeleter.cpp` | Log deletion tool |
| `lan_interface.cpp` | LAN network view |
| `lanmonitor.cpp` | LAN state tracking |

## Build Instructions

```bash
# Set environment
export UPLINKROOT=/path/to/uplink-source-code

# Build dependencies 
cd $UPLINKROOT/contrib && make

# Build libraries
cd $UPLINKROOT/lib && make

# Build game
cd $UPLINKROOT/uplink/src && make

# Output: uplink.full in src/, or bin/uplink for runnable version
```

## UplinkOS Mod Integration

The UplinkOS mod (Windows, Allegro5) contains bug fixes. Key fixes have been integrated:

### ✅ Already Integrated
- Bank transfer field validation (`scriptlibrary.cpp:Script10`)
- Stock account creation validation (`scriptlibrary.cpp:Script12`)
- File download disconnect handling (`filecopier.cpp`)
- Voice analyzer disconnect handling (`voiceanalyser.cpp`)

### ⚠️ UplinkOS-Specific (Not Applicable)
Most UplinkOS fixes are for its Allegro5 UI rewrite and don't apply to the original SDL/OpenGL codebase:
- UI scaling fixes
- Task Manager v2
- Hotkey externalization
- Tour Guide feature

### Reference Files
- `uplink/mod/UplinkOS_ReadMe.txt` - Complete changelog with all fixes
- `uplink/mod/UplinkOS.exe.c` - Decompiled mod code (hard to read)

## Common Tasks

### Finding Function Implementations
```bash
grep -rn "FunctionName" uplink/src/
```

### Comparing with Decompiled Mod
The decompiled code uses mangled names. Search for **string literals** instead:
```bash
grep -n "error message text" uplink/mod/UplinkOS.exe.c
```

### Building for Linux
Requires: OpenGL, SDL, FTGL, libjpeg, and other dependencies listed in BUILDING.linux

## Known Issues

1. **Copy protection** - May need to disable for testing (see app/app.cpp)
2. **FTGL dependency** - May need manual configuration

## Related Documentation

- `docs/MOD_FIXES.md` - Detailed mod fix tracking
- `BUILDING.linux` - Build instructions
- `.agent/workflows/` - Agent workflow files (if any)
