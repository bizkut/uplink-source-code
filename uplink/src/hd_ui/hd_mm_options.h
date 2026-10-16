#ifndef _included_hd_mm_options_h
#define _included_hd_mm_options_h

#include "hd_ui_object.h"
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace HDUI {

// ownerRoot must outlive this controller. Attached trees belong exclusively to
// ownerRoot; hidden trees are retained here, including a dismissed popup whose
// button callback may still be returning through the input dispatcher.
class HDMMOptions {
public:
  using ProfileSelected =
      std::function<void(const std::string &, const std::string &)>;
  using NewUserRequested = std::function<void()>;
  using ProfileRetired = std::function<void(const std::string &)>;

  HDMMOptions(UIObject &ownerRoot, ProfileSelected profileSelected,
              NewUserRequested newUserRequested, ProfileRetired profileRetired);
  ~HDMMOptions();
  HDMMOptions(const HDMMOptions &) = delete;
  HDMMOptions &operator=(const HDMMOptions &) = delete;
  HDMMOptions(HDMMOptions &&) = delete;
  HDMMOptions &operator=(HDMMOptions &&) = delete;

  bool Show();
  void Hide();
  bool IsVisible() const;
  bool HandleInput(const ALLEGRO_EVENT &event);
  bool IsPopupVisible() const;

private:
  struct Settings {
    int width = 0;
    int height = 0;
    bool fullscreen = false;
    bool music = false;
    bool softwareMouse = false;
  };
  struct ProfileRow {
    std::string username;
    UIObject *container = nullptr;
    UITextPoint *caption = nullptr;
    UIButtonStandard *select = nullptr;
    UIButtonStandard *remove = nullptr;
  };

  bool LoadLayouts();
  void SnapshotSettings();
  void PopulateResolutions();
  void UpdateResolution();
  void CycleResolution();
  void RefreshProfiles();
  void SelectProfile(std::size_t row);
  void RequestNewUser();
  void ConfirmRemoval(std::size_t row);
  void RemoveProfile();
  void HidePopup();
  void Apply();
  std::string CurrentUsername() const;

  UIObject &ownerRoot;
  ProfileSelected profileSelected;
  NewUserRequested newUserRequested;
  ProfileRetired profileRetired;
  std::unique_ptr<UIObject> panelTree;
  std::unique_ptr<UIObject> popupTree;
  UIObject *panel;
  UIObject *popup;
  UIButtonStandard *resolution;
  UITextPoint *resolutionCaption;
  UIButtonSelect *fullscreen;
  UIButtonSelect *music;
  UIButtonSelect *softwareMouse;
  std::array<ProfileRow, 4> rows;
  std::vector<std::pair<int, int>> screenModes;
  std::size_t selectedResolution;
  Settings snapshot;
  int popupDismissKey;
  std::string selectedProfile;
  std::string removalUsername;
};

} // namespace HDUI

#endif
