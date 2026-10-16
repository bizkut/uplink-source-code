#ifndef _included_hd_remote_services_h
#define _included_hd_remote_services_h

#include "hd_ui_object.h"
#include <memory>
#include <string>

namespace HDUI {

// Projects the current live remote service into its GOLD layout. The owner is
// the 1920x1080 input root and must outlive this controller.
class HDRemoteServices {
public:
  explicit HDRemoteServices(UIObject &ownerRoot);
  ~HDRemoteServices();
  HDRemoteServices(const HDRemoteServices &) = delete;
  HDRemoteServices &operator=(const HDRemoteServices &) = delete;

  void Update();
  void Hide();
  bool IsVisible() const;
  bool Invoke(const std::string &command);
  bool HandleModalInput(const ALLEGRO_EVENT &event);
  bool HandleInput(const ALLEGRO_EVENT &event);

private:
  struct State;
  std::unique_ptr<State> state;
};

} // namespace HDUI
#endif
