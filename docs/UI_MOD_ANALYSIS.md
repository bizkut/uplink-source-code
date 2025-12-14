# UplinkOS Mod - UI Analysis

Analysis of UI modifications in the UplinkOS mod folder for potential integration.

## Mod Structure Overview

```
uplink/mod/
├── UplinkOS.exe.c          # Decompiled mod (801k lines)
├── UplinkOS_ReadMe.txt     # Complete changelog
├── data/
│   ├── gatewaydefs.txt     # Gateway hardware definitions ⭐
│   ├── companyb.txt        # Company data
│   └── gateways/           # Gateway images
│       └── lans/           # LAN layouts
└── uplinkHD/
    ├── default_hotkeys.xml # Hotkey bindings ⭐
    ├── agentaliases.txt    # Agent name aliases
    ├── fornamesM/F.txt     # First names
    ├── surnames.txt        # Last names
    ├── fonts/              # Custom fonts (Aero Matics)
    ├── graphics/           # Texture atlases, portraits
    ├── sounds/             # New sounds (51 files)
    └── layouts/            # XML UI system (102 files)
        ├── HUD/            # 89 HUD layouts
        │   ├── apps/       # 18 app UIs
        │   ├── interfaces/ # 43 server screens
        │   └── windows/    # 18 window types
        └── MainMenu/       # Menu screens
```

## ⭐ Potentially Portable Assets

### 1. Gateway Definitions (`data/gatewaydefs.txt`)

Custom gateway hardware specs. Could be added to original game's gateway data.

**Format:**
```
Name, Cost, MaxCPU, MaxMem, MaxUpg, MaxSec, Bandwi, Width, Height, Description
cpu     X Y     (position slots)
mem     X Y     (position slots)
sec     X Y     (position slots)
```

**New Gateways Defined:**
- Gateway ALPHA /CPU - Extra CPU slot
- Gateway ALPHA /MEMORY - Double memory
- KRONOS Corp 80-860 - Security slot
- ZION-780 Workstation - All rounder
- UNITY High-Security - 4 security slots
- AM-CORE File Server - 32 memory slots
- OMEGA Parallel Supercomputer - 16 CPUs
- TRINITY-1686a - Best all-rounder

### 2. Hotkeys (`uplinkHD/default_hotkeys.xml`)

Externalized hotkey bindings - could be adapted for original:

| Hotkey | Action |
|--------|--------|
| Ctrl+E | Open E-Mail |
| Ctrl+Tab | Open App Menu |
| Ctrl+F | Open Files Manager |
| Ctrl+L | Open LAN View |
| F1 | Internal Help |
| F2 | Status |
| F3 | Gateway Info |
| Ctrl+N | View Notes |
| F5-F8 | Game Speed 0-3 |
| Ctrl+L | Log Out |
| Ctrl+Q | Quit |

### 3. Name Lists

- `fornamesM.txt` - Male first names
- `fornamesF.txt` - Female first names  
- `surnames.txt` - 500+ surnames (cleaned of accent characters)
- `agentaliases.txt` - Pre-made agent handles

### 4. Sounds (`uplinkHD/sounds/`)

51 new sound files - could be added to original data.

## ❌ Not Portable (Allegro5-Specific)

### XML Layout System

The `layouts/` folder contains 102 XML files defining UI:

```xml
<ButtonStandard name="btnContinue" x="629" y="663" w="160" h="30">
  <Rectangle name="gfxHover" fillColor="2BAAFF" fillA="0.5"/>
  <TextPoint font="AeroMaticsLightRegular" size="24" text="Continue"/>
</ButtonStandard>
```

This is a **custom XML-based UI framework** for Allegro5 that:
- Defines all buttons, text, containers programmatically
- Uses texture atlases (`uplinkHD_atlas_00.png`)
- Requires custom font (Aero Matics)
- Has its own rendering/event system

**Cannot be directly used** with original SDL/OpenGL codebase without major rewrite.

### Graphics/Portraits

- NPC portrait system (8.8MB of portraits)
- Texture atlases for HD graphics
- These are tied to the XML UI system

## Recommendations

### Low Effort (Data Only)
1. ✅ Add new gateway definitions from `gatewaydefs.txt`
2. ✅ Use cleaned name lists (`surnames.txt` etc.)
3. ✅ Add new sounds from `sounds/`

### Medium Effort (Code Changes)
1. ⚠️ Implement hotkey system inspired by `default_hotkeys.xml`
2. ⚠️ Port LAN improvements if identifiable in decompiled code

### High Effort (Major Rewrite) - IN PROGRESS ✅
1. ✅ XML UI system - Framework created (`hd_ui/hd_layout_parser.cpp`)
2. ✅ HD graphics - Allegro5 layer created (`hd_ui/hd_allegro5.cpp`)
3. ⏳ Screen migration (MainMenu, TopBar, TaskBar) - Pending

## Files Created

| File | Purpose |
|------|---------|
| `hd_ui/hd_allegro5.cpp/h` | Allegro5 display, events, primitives |
| `hd_ui/hd_ui_object.cpp/h` | Widget classes (Container, Gradient, etc.) |
| `hd_ui/hd_layout_parser.cpp/h` | TinyXML2 XML layout parser |

## Files for Reference

- Mod changelog: `uplink/mod/UplinkOS_ReadMe.txt`
- Decompiled source: `uplink/mod/UplinkOS.exe.c`
- Gateway defs: `uplink/mod/data/gatewaydefs.txt`
- Hotkeys: `uplink/mod/uplinkHD/default_hotkeys.xml`

