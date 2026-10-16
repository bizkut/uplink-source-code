#ifndef _included_hd_map_lan_h
#define _included_hd_map_lan_h

#include "hd_ui_object.h"
#include <memory>
#include <string>

namespace HDUI {

// Owns the map, LAN canvas and transient connection/mail HUD views. The input
// root outlives this controller. Update follows live world and legacy screens;
// link-list controllers can supply an IP without retaining a VLocation pointer.
class HDMapLan {
public:
  explicit HDMapLan(UIObject &ownerRoot);
  ~HDMapLan() noexcept;
  HDMapLan(const HDMapLan &) = delete;
  HDMapLan &operator=(const HDMapLan &) = delete;

  void Update();
  void Hide();
  bool IsVisible() const;
  bool ShowMap();
  bool ShowLAN();
  bool IsMapVisible() const;
  bool IsLANVisible() const;
  bool CapturesKeyboard() const;
  void SetContextLocation(const std::string &ip);
  bool Invoke(const std::string &command);
  bool Invoke(const std::string &command, const std::string &ip);
  // Route before hotkeys so confirmation dialogs and map context menus are
  // modal. Other pointer input is dispatched by the shared owner root.
  bool HandleInput(const ALLEGRO_EVENT &event);

private:
  struct State;
  std::unique_ptr<State> state;
};

} // namespace HDUI
#endif
