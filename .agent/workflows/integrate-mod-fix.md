---
description: How to integrate UplinkOS mod fixes
---

## Integrating Mod Fixes

1. Find the fix in the decompiled mod code:
```bash
grep -n "search_term" /home/bizkut/Downloads/uplink-source-code/uplink/mod/UplinkOS.exe.c
```

2. Locate corresponding original source file in `uplink/src/`

3. Compare logic and add fix with comment:
```cpp
// [UPLINKOS MOD FIX] Description of fix
// ... modified code ...
```

4. Mark as complete in task.md

## Key Files
- `game/scriptlibrary.cpp` - Script handlers (transfers, account creation)
- `interface/taskmanager/` - App implementations
- `world/computer/bankaccount.cpp` - Bank logic
- `interface/localinterface/lan_interface.cpp` - LAN system

## Decompiled Code Reference
- `uplink/mod/UplinkOS.exe.c` - Full decompiled mod (~800k lines)
- Look for `HD_` prefixed classes (mod's wrapper classes)
