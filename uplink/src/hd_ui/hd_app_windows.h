#ifndef _included_hd_app_windows_h
#define _included_hd_app_windows_h

#include "hd_ui_object.h"
#include <memory>
#include <string>

namespace HDUI {

class HDHotkeyManager;
// Value-only observation: no task, wrapper, screen or model pointer survives a
// callback. PID identity is re-resolved on every observation and action.
struct HDAppTaskSnapshot {
  int pid = -1;
  std::string name, target, statusText, detail;
  float version = 1;
  double priority = 0;
  float progress = 0;
  int status = 0;
  bool running = false, complete = false, failed = false;
};
bool ReadAppTask(int pid, HDAppTaskSnapshot &snapshot);

// The selected-row caller validates its context; ownership, executable version,
// game session and final instance limits are revalidated here before creation.
int LaunchOwnedApp(const std::string &name);
// Built-in file actions do not require an owned executable. Other names fail;
// the real TaskManager still enforces its shared instance limits.
int LaunchBuiltinApp(const std::string &name);
// Called by TaskManager after creation/retargeting, including legacy launches.
// Stores the GOLD creation-time minimum in the scheduler, not in a widget.
void RefreshAppScheduling(int pid);
int AppInstanceLimit(const char *name);

class HDAppManager {
public:
  explicit HDAppManager(UIObject &ownerRoot);
  ~HDAppManager();
  HDAppManager(const HDAppManager &) = delete;
  HDAppManager &operator=(const HDAppManager &) = delete;
  void Update();
  void Hide();
  bool IsVisible() const;
  bool ShowMenu();
  void HideMenu();
  bool Launch(const std::string &name, bool contextual = false);
  bool Invoke(const std::string &command);
  bool HandleInput(const ALLEGRO_EVENT &event);
  // Borrowed; the manager owning these bindings must outlive this controller.
  void SetHotkeys(const HDHotkeyManager &hotkeys);
  // Register exact target-defined app names, not contextual commands like LAN Scan.
  // Call during the parent's action setup; do not invoke after destroying us.
  void RegisterHotkeyActions(HDHotkeyManager &hotkeys);

private:
  class Impl;
  std::unique_ptr<Impl> impl;
};

} // namespace HDUI
#endif
