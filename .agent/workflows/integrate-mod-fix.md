---
description: How to integrate UplinkOS mod fixes
---

# Integrate Mod Fix

// turbo-all

## Steps

1. **Search decompiled mod**
```bash
grep -n "search_term" /home/bizkut/Downloads/uplink-source-code/uplink/mod/UplinkOS.exe.c
```

2. **Find original source** in `uplink/src/`

3. **Apply fix with comment**
```cpp
// [UPLINKOS MOD FIX] Description
// ... modified code ...
```

4. **Update docs** - Mark complete in `docs/MOD_FIXES.md`

## Key Locations
| Area | Files |
|------|-------|
| Scripts | `game/scriptlibrary.cpp` |
| Apps | `interface/taskmanager/*.cpp` |
| Bank | `world/computer/bankaccount.cpp` |
| LAN | `interface/localinterface/lan_interface.cpp` |

## Reference
- Decompiled: `uplink/mod/UplinkOS.exe.c` (~800k lines)
- Look for `HD_` prefixed classes
