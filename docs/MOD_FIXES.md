# UplinkOS Mod Fixes to Integrate

This document tracks which fixes from the UplinkOS mod have been integrated into the Linux port.

## ✅ Completed

| Fix | File | Description |
|-----|------|-------------|
| Bank Transfer Validation | `scriptlibrary.cpp:Script10` | Check all fields non-empty before transfer |
| Stock Account Validation | `scriptlibrary.cpp:Script12` | Add username/password validation |

## 🔄 In Progress

None currently.

## ⏳ Pending - Critical

| Fix | Original File | Notes |
|-----|---------------|-------|
| Log deletion crash | `logdeleter.cpp` | Null pointer check |
| Password screen crash | `passwordbreaker.cpp` | Bounds check |

## ⏳ Pending - High Priority

| Fix | Original File | Notes |
|-----|---------------|-------|
| Password Breaker timing | `passwordbreaker.cpp` | Timing sync |
| Voice Analyzer timing | `voiceanalyser.cpp` | Timing sync |
| File download disconnect | `filecopier.cpp` | Connection state |
| LAN lock states | `lan_interface.cpp` | State management |

## ⏳ Pending - Nice to Have

| Fix | Notes |
|-----|-------|
| Disk space validation | Check before operations |
| Hotkey externalization | XML config file |
| Notes system | New feature |
| Tour guide | New feature |

## Source Reference

The full categorized list is in the brain artifacts:
- `mod_fixes_enhancements.md` - 116 fixes categorized
- `code_change_analysis.md` - How to find changes in decompiled code
