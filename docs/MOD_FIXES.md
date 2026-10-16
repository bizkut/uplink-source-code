# UplinkOS GOLD Reconstruction Ledger

## Target authority

| Property | Value |
|---|---|
| Target | `uplink/mod/UplinkOS.exe` |
| SHA-256 | `4461a546b74f19ea3874a72723c4c743731c5b84de09c209950e85684e1a7fd8` |
| Format | PE32/i386, 4,712,448 bytes, Visual C++ linker 12.0 |
| Build date in package | UplinkOS GOLD v1.04, 03.03.2017 |
| Decompiler | `uplink/mod/UplinkOS.exe.c` |
| Assets | `uplink/mod/uplinkHD/` and runtime copy `uplink/bin/uplinkHD/` |

The target is conventional, not packed: normal PE sections, RTTI, source-path strings, and complete pseudocode are present. IDA Free 9.4 plus `ida-free-mcp` was used to validate function boundaries/xrefs and annotate recovered menu functions. IDA Free has no Hex-Rays decompiler; behavioral recovery therefore uses the supplied final pseudocode plus disassembly/xrefs.

## Implemented binary modifications

### Save/model cutover

| Version | Recovered field |
|---|---|
| SAV64 | `Message::read` and `Mission::read` |
| SAV65 | `Person::gender` |
| SAV66 | serialized e-mail attachment |
| SAV67 | mission target label |

All writers/readers use `SAVEFILE_VERSION == SAV67` and accept SAV56–SAV67. Message attachments are deep-copied and owned. Viewing mail/missions updates read state. Attachments transfer to the gateway only through the explicit e-mail action, with capacity failure feedback; receiving a message no longer silently consumes the attachment.

### World, people, and missions

- World and new-game anchors recovered as 24 February 2026 and 24 March 2026.
- New-game generation drains overdue scheduled events before play starts.
- Male/female name lists and persisted person gender drive mission pronouns.
- Generated missions populate the recovered target label.
- The employer temporary upload account has security level 2.
- Steal-all-files completion derives target timestamps from mission creation without changing the serialized completion tuple.
- New-agent creation is shared between legacy and HD registration and creates the final welcome message, access code, bank account, records, tutorial mission, and `Uplink Test Machine` target.

### Security/task fixes

- Remote file deletion requires a connected remote file, sufficient access, and a disabled firewall; local deletion is not subjected to remote guards.
- Log deletion authorizes before mutating state and accepts admin or authenticated account-log context.
- Upload capacity failures are retryable and allocation-safe.
- Password Breaker invokes the real password/user-ID submit path when complete.
- Attachment and file-copy ownership leaks were removed.

### Application scheduler and catalog

- The software catalog now matches the PE table exactly: 71 rows, including
  `HUD_ConnectionRandomizer` and `HUD_TaskManager_V2.0`; the four rows absent
  from the target were removed. The 19 hardware rows also match exactly.
- Recovered HUD upgrade bits are 1 (trace map), 2 (connection analysis),
  4 (IRC), 8 (LAN), 16 (connection randomizer), and 32 (Task Manager V2).
- Active-app limits, per-PID minimum CPU shares, self-removal-safe task cycles,
  and the recovered game-speed factors are model state rather than UI samples.
- Gateways without bit 32 use `WinTaskManager.xml`; purchasing bit 32 switches
  to `WinTaskManagerV2.xml` with pinning and balance controls.

### HD UI runtime

Implemented under `uplink/src/hd_ui/`:

- Borrowed Allegro 5 display facade, one presentation per frame, scaled
  drawing/input, fonts/images/primitives, strict TinyXML2 layout parsing, and
  atlas loading.
- Exclusive UI-tree ownership, retained layout attachment, clipping, z order,
  focus/capture, Tab navigation, single/multiline editing, caret scrolling,
  authored horizontal/vertical scrollbars, draggable windows, delayed
  hotkey-aware tooltips, modal popups, and mirrored Eclipse message boxes.
- Final eight-step onboarding, main login, recent-profile/options flow,
  keyboard-contained shutdown confirmation, GOLD version label, top bar, task
  bar, Notes, and both Task Manager generations.
- Local windows: App Menu, E-Mail/missions/composition/attachments, Files
  Manager with target context actions, the eight-entry System menu,
  Options, System/Gateway, User status/finance, Tour Guide, Analyser, Cheats
  (test builds), and View Gateway.
- Remote computers: Message, Menu, Dialog, Password, User ID, Disconnected,
  High Security, Cypher, files, logs, links, BBS, news, shares, records,
  accounts/loans, academic/criminal/social-security/security databases,
  software/hardware stores, contacts, company/ranking/gateway services,
  Console, Voice, Faith, Protovision, Nuclear War, and Radio Transmitter.
- Hacking applications: all target-defined `HD_*` app skins (password and
  dictionary attacks, Decypher, disable/bypass tools, IP tools, Log Modifier,
  Voice Analyser, trace/revelation trackers, Motion Sensor, Gateway Nuke, and
  Uplink Agent List), backed by the original task objects.
- Browser transition, world map and context menu, LAN topology and controls,
  notifications, Loading, Obituary, Connection Lost, Game Over variants, and
  The Team.
- XML default hotkeys retain the target lock-modifier and immutable-binding
  collision semantics; user files remain under `app->userpath`.

### Build/runtime repairs

- `Makefile.linux` compiles all HD modules and defines `USE_ALLEGRO5`.
- FTGL's default goal builds the library rather than cleaning it.
- Tracked object/archive/executable products were removed and ignored.
- Graphics shutdown, screen restoration, Eclipse rendering, Allegro input observation, and one-buffer-flip ownership were repaired.
- New profiles are committed before first-session quit; the autosave timestamp is initialized.
- `App::SaveGame` normalizes paused sessions for serialization and restores live pause, preventing empty-save truncation for quit, logout, and every other caller.
- Retiring a profile invalidates cached Notes/hotkeys so deleted auxiliary files cannot be recreated at shutdown.
- Legacy load-failure dialogs temporarily take foreground rendering/input instead of being hidden by HD login.
- The advertised F1 command connects and authenticates to Internal Services Help screen 20.

## Verification performed

Ubuntu 24.04 container, native Linux executable:

1. Built bundled FTGL, tcp4u, irclib, unrar, all internal libraries, and
   `uplink/src/uplink.full`; installed that exact binary under `uplink/bin`.
2. Launched the installed binary under Xvfb and kept the startup/login surface
   alive past the pre-world phase.
3. Exercised all first-run pages and the empty-username error popup.
4. Created an agent; observed encrypted `.usr` and `.tmp` SAV67 files
   immediately, quit, restarted, and loaded the profile back to the gateway.
5. Opened the HD options panel and selected a recent profile, producing the
   recovered sentinel password.
6. Opened Notes, edited title/body, closed it, and observed the exact XML.
7. Opened the basic Task Manager without bit 32 and observed its empty state;
   the V2 empty state and controls were exercised separately.
8. Advanced the gateway boot Dialog into the Links screen and observed the HD
   layouts with the legacy workspace masked while top/task bars stayed active.
9. Opened Status through F2, App Menu, Files Manager, E-Mail/mission details,
   and the large World Map; exercised close/toggle and modal input ownership.
10. Observed a delayed top-bar tooltip and all eight System-menu entries, then
    opened Files Manager while simulation time was paused.
11. Exercised global logout and the main-menu shutdown confirmation; pressing
    Enter on its default Cancel action returned to login without submitting the
    underlying password form.
12. Paused the live game, logged out, observed intact nonempty `.usr/.tmp`
    files, and loaded the profile again.
13. Opened F1 Internal Help and observed the authenticated Help menu.
14. Retired a profile with cached Notes and confirmed profile/notes/hotkey
    files remained deleted after shutdown.
15. Loaded a deliberately truncated profile and observed the foreground
    compatibility dialog.
16. Repeated a clean rebuild after final review, installed the resulting
    executable, and completed a final load, gateway, HUD/window, logout,
    modal-cancel, save, and clean-exit run. That session added no `HD_UI`,
    Redshirt, assertion, or segmentation-fault diagnostics.
17. In an isolated copy of the profile, invoked the production
    `Gateway::GiveAllSoftware` path under GDB to expose the complete purchased
    inventory. Observed the populated App Menu, started and retired File Copier
    through Task Manager V2, opened the upgraded LAN view, and inspected the
    finance ledger.
18. Triggered `Game::GameOver` in that isolated session, observed the HD
    disavowal transition, reloaded the resulting dead-agent save, and observed
    the HD obituary. This exposed and fixed legacy end-screen text bleeding
    through the GOLD layout; the repeated run rendered only the HD surfaces.
19. Performed a final clean library/application rebuild after the end-screen
    fix, installed that exact `uplink.full`, launched it from `uplink/bin`, and
    exited normally from the reconstructed shutdown flow.
20. With both IRC and LAN-view upgrades owned in an isolated profile, opened
    the legacy IRC screen, focused `irc_inputbox`, and sent a real `x` key
    through X11. The field rendered `x`; GDB readback reported screen 16
    (`SCREEN_IRC`), caption `" x"`, and no `lan_cancel` button. The key was
    therefore text input and did not invoke `LAN Reset` or replace IRC.
21. Exported the staged Git index into an empty directory, followed the full
    `BUILDING.linux` prerequisite sequence, installed the resulting executable,
    launched it with an empty home directory, observed the HD first-run surface,
    and exited normally with status 0.

Audio initialization reported no ALSA device in the headless container;
rendering, input, saving, loading, and clean shutdown continued.

## Residual scope

This is not a claim of byte-for-byte equivalence with the 2017 Windows PE. The
Linux compiler, ABI, libraries, and executable format differ. Screens for which
the target defines an `HD_*` class and final layout now have source
controllers; the original Eclipse objects remain the authoritative game model
and callback backend. Target screens with no HD counterpart (for example the
code-card and nearest-gateway interfaces) intentionally retain their original
renderer. The supplied PE and decompiler output remain authoritative when
auditing edge-case parity.
