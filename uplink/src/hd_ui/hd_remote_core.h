#ifndef _included_hd_remote_core_h
#define _included_hd_remote_core_h

#include "hd_ui_object.h"
#include <memory>
#include <string>

namespace HDUI {

// Owns the final browser surface; Eclipse remains the live behavior/program
// backend. Route the manager's modal mirror first, then HandleInput before
// legacy input and normal HD dispatch.
class HDRemoteCore {
public:
  explicit HDRemoteCore(UIObject &ownerRoot);
  ~HDRemoteCore();
  HDRemoteCore(const HDRemoteCore &) = delete;
  HDRemoteCore &operator=(const HDRemoteCore &) = delete;

  void Update();
  void Hide();
  bool IsVisible() const;
  bool Invoke(const std::string &command);
  bool HandleInput(const ALLEGRO_EVENT &event);

private:
  struct State;
  std::unique_ptr<State> state;
};

} // namespace HDUI
#endif
