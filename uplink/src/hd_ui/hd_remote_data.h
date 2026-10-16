#ifndef _included_hd_remote_data_h
#define _included_hd_remote_data_h

#include "hd_ui_object.h"
#include <memory>
#include <string>

namespace HDUI {

// Projects the current remote model into the seven GOLD data-screen layouts.
// The owner root must outlive this controller. Update follows real remote
// navigation; Hide only detaches the projection, never the legacy/model screen.
class HDRemoteData {
public:
  explicit HDRemoteData(UIObject &ownerRoot);
  ~HDRemoteData();
  HDRemoteData(const HDRemoteData &) = delete;
  HDRemoteData &operator=(const HDRemoteData &) = delete;

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
