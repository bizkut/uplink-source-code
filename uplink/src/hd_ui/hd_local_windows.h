#ifndef _included_hd_local_windows_h
#define _included_hd_local_windows_h

#include "hd_ui_object.h"
#include <memory>
#include <string>

namespace HDUI {

// Independent GOLD local windows. The shared input root must outlive this
// controller; Invoke changes only the addressed window, not its neighbours.
// The active mirrored legacy screen/draft remains authoritative for tutorials.
class HDLocalWindows {
public:
  explicit HDLocalWindows(UIObject &ownerRoot);
  ~HDLocalWindows();
  HDLocalWindows(const HDLocalWindows &) = delete;
  HDLocalWindows &operator=(const HDLocalWindows &) = delete;

  void Update();
  void Hide();
  bool IsVisible() const;
  bool Invoke(const std::string &command);
  // Route before the shared root dispatcher. Handles modal popups/menus and
  // raises clicked windows without dispatching the same pointer event twice.
  bool HandleInput(const ALLEGRO_EVENT &event);

private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};

} // namespace HDUI
#endif
