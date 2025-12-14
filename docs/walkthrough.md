# UplinkOS Mod Recreation - Walkthrough

## 🎯 Objective
Integrate UplinkOS mod bug fixes into the original Uplink Linux source code.

## ✅ Build Status
- **Binary:** `uplink/bin/uplink`
- **Compiled:** Dec 14, 2025
- **Status:** Working ✅

## 🔧 Fixes Applied

### Phase 1: Bug Fixes (10/10 ✅)

| Fix | File | Change |
|-----|------|--------|
| Password_Breaker pause | `passwordbreaker.cpp` | Skip Tick when paused |
| Decypher pause | `decypher.cpp` | Skip Tick when paused |
| Voice_Analyzer pause | `voiceanalyser.cpp` | Skip Tick when paused |
| LAN lock state | `lan_interface.cpp` | Color border (red/green) |
| File delete security | `filedeleter.cpp` | Block if Proxy/Firewall active |
| Bank loan interest | `notificationevent.cpp` | Delay first charge 30 days |
| Bank transfer validation | Already in source | ✅ |
| Stock account validation | Already in source | ✅ |
| File download disconnect | Already in source | ✅ |
| Connection reset check | Already in source | ✅ |

### Phase 2: Gameplay (3/5 ✅)

| Fix | File | Change |
|-----|------|--------|
| Log_Deleter v3 skip deleted | `logdeleter.cpp` | Don't overwrite with deleted log |
| Password auto-submit | `passwordbreaker.cpp` | Auto-click 'Proceed' after cracking |
| StealAllFiles fix | `missiongenerator.cpp` | Add date to filenames to prevent collisions |
| Mission frequency | `data.h` | Frequency 12h -> 8h |
| Readonly accounts | `Task Manager` | Prevent File/Log deletion and Upload for security > 2 |

### Phase 3: Assets (Already integrated)
- Gateway definitions: Identical ✅
- Name lists: Identical ✅
- Company suffixes: Present ✅

## 📝 Key Files Modified

render_diffs(file:///home/bizkut/Downloads/uplink-source-code/uplink/src/interface/taskmanager/passwordbreaker.cpp)

render_diffs(file:///home/bizkut/Downloads/uplink-source-code/uplink/src/interface/taskmanager/filedeleter.cpp)

render_diffs(file:///home/bizkut/Downloads/uplink-source-code/uplink/src/interface/localinterface/lan_interface.cpp)

render_diffs(file:///home/bizkut/Downloads/uplink-source-code/uplink/src/world/scheduler/notificationevent.cpp)

render_diffs(file:///home/bizkut/Downloads/uplink-source-code/uplink/src/world/generator/missiongenerator.cpp)

## 🏁 Summary
**Total fixes applied:** 9 code changes
**Build status:** ✅ Compiles and runs
**Game:** Fully functional with mod bug fixes
