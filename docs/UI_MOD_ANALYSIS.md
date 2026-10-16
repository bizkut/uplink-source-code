# UplinkOS GOLD HD UI Analysis

## Final asset inventory

`uplink/bin/uplinkHD/` contains fonts, atlases/images, sounds, name lists,
default hotkeys, and XML layouts for main-menu, HUD, windows, applications, and
remote interfaces. The target executable contains RTTI for 126 `HD_*` classes.
The XML is data; behavior lives in the executable callbacks.

## Reconstructed runtime

| Layer | Implementation |
|---|---|
| Display/events | `hd_allegro5.*`, borrowing Gucci's Allegro display and queue |
| Widget tree | `hd_ui_object.*`, exclusive child ownership and borrowed input links |
| XML/assets | `hd_layout_parser.*`, `hd_atlas.*` |
| Ownership/backend bridge | `hd_legacy_bridge.*` |
| Orchestration/HUD | `hd_screens.*` |
| Hotkeys | `hd_hotkeys.*` |
| First run/main menu | `hd_intro.*`, `hd_mm_options.*` |
| Local windows | `hd_local_windows.*`, `hd_notes.*`, `hd_taskmanager_v2.*` |
| Applications | `hd_app_windows.*` |
| Remote computers | `hd_remote_core.*`, `hd_remote_data.*`, `hd_remote_services.*` |
| Map/LAN/browser | `hd_map_lan.*` |
| End screens | `hd_end_screens.*` |

Gucci presents once after Eclipse and HD rendering. HD input is offered first;
consumed events do not reach Eclipse. A 1920x1080 logical root scales drawing
and hit testing to the actual display. Retained layout hosts detach rather than
destroy themselves during callbacks, so a legacy action may replace its screen
without invalidating the callback stack.

The original Eclipse objects remain live as the game model and callback
backend. HD controllers resolve legacy buttons at the point of use, mirror
editable values, invoke the original `MouseUp`/screen methods, and never retain
a `Button *` across a callback. Full remote HD surfaces mask only the legacy
workspace; the HD top/task bars and compact legacy world map remain usable.
Full-screen loading and ending layouts mask the complete Eclipse surface while
retaining its screen object as the transition/callback backend.

## Migrated surfaces

- Eight-step first-run flow, login, recent profiles, deletion, options,
  loading, keyboard-contained shutdown, compatibility popups, and GOLD version
  display.
- Top/task bars, delayed hotkey-aware tooltips, authored list scrollbars,
  default/per-user hotkeys, Notes, basic Task Manager, and Task Manager V2.
- App Menu, E-Mail/missions/composition, Files Manager and context actions,
  the System menu, in-game Options, System/Gateway, User/Finance, Tour Guide,
  Analyser, Cheats, and View Gateway.
- Every target-defined HD remote class: core authentication/menu/dialog
  screens, data lists, stores, records/accounts, contacts/company/ranking,
  gateway changes, Console, Voice, Faith, Protovision, Nuclear War, and Radio.
- Every target-defined HD hacking-app class, backed by the original task model.
- Browser transition, full World Map/context menu, LAN topology and controls,
  mail/mission notifications.
- Loading, Obituary, Connection Lost, all Game Over variants, and The Team.

## Intentional legacy backend/fallback

The HD port does not duplicate game simulation. File/log mutation, purchases,
records, connections, task execution, missions, and plot transitions still run
through their original classes. That is a backend boundary, not an unmigrated
visual surface.

Screens for which the target itself has no `HD_*` class/layout continue using
Eclipse, including code-card and nearest-gateway interfaces. The compact world
map also remains visible until its target-defined full-screen HD map is opened.

## Recovered hotkey semantics

`default_hotkeys.xml` is loaded before the optional per-user file. Existing
`canOverwrite=0` bindings cannot be displaced. Caps/Num/Scroll lock bits are
ignored during matching. Global actions remain available over remote screens;
focused HD text fields own ordinary editing keys, while Enter/Escape retain the
active screen's semantics. Context commands dispatch through the active
map/LAN/file/log/application controller and fall through only when no valid
target exists.
