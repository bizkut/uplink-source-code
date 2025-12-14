---
description: How to work on the HD UI port
---

## HD UI Development

// turbo-all

### Key Files
- `uplink/src/hd_ui/` - HD UI framework (Allegro5)
- `uplink/mod/UplinkOS.exe.c` - Decompiled mod (search for `HD_`)
- `uplink/mod/uplinkHD/layouts/` - XML layout files
- `uplink/bin/uplinkHD/` - Runtime assets (copied layouts, fonts, atlas)

### Build
```bash
cd /home/bizkut/Downloads/uplink-source-code/uplink/src
make -f Makefile.linux
```

### HD UI Files
| File | Purpose |
|------|---------|
| `hd_allegro5.cpp/h` | Allegro5 graphics layer |
| `hd_ui_object.cpp/h` | Widget classes |
| `hd_layout_parser.cpp/h` | TinyXML2 XML parser |
| `hd_screens.cpp/h` | Screen classes (MainMenu, TopBar, TaskBar) |
| `hd_atlas.cpp/h` | Texture atlas loader |

### Reference Mod Classes
Search decompiled code for:
```bash
grep -n "HD_UI_Object\|HD_MainMenu\|HD_TopBar" uplink/mod/UplinkOS.exe.c
```

### Adding New Widgets
1. Add class to `hd_ui_object.h/cpp`
2. Add parser case in `hd_layout_parser.cpp`
3. Rebuild
