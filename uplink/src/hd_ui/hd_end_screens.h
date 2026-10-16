#ifndef _included_hd_end_screens_h
#define _included_hd_end_screens_h

#include "hd_legacy_bridge.h"
#include <string>

class MainMenuScreen;

namespace HDUI {

// Presentation follows MainMenu; scripts 90/91, App and HDIntro retain sole
// ownership of loading, new-game creation and the transition into the HUD.
class HDEndScreens {
public:
  explicit HDEndScreens(UIObject &ownerRoot);
  ~HDEndScreens();
  HDEndScreens(const HDEndScreens &) = delete;
  HDEndScreens &operator=(const HDEndScreens &) = delete;

  void Update();
  void Hide();
  bool IsVisible() const;
  // Back, PI and Awards. Back obeys the legacy staged return-button timing.
  bool Invoke(const std::string &command);
  // Route before hotkeys/Eclipse. Pointer input otherwise uses ownerRoot.
  bool HandleInput(const ALLEGRO_EVENT &event);

private:
  enum class PopupKind { None, LoadError, Award };

  HDLayoutView *ViewForScreen(int screen);
  bool Prepare(HDLayoutView &view);
  void HideScreen();
  void FocusBack();
  void UpdateLoading();
  void UpdateObituary();
  void UpdateGameOver();
  void UpdateConnectionLost();
  void UpdateLoadError(bool failedLoad, MainMenuScreen *screen);
  bool Back();
  bool ShowTeam();
  bool ShowAwards();
  void ShowAward(unsigned bit);
  void ClosePopup();
  const char *ReturnButton() const;

  UIObject &ownerRoot;
  HDLayoutView loading, obituary, gameOver, connectionLost, team;
  HDPopupController popup;
  HDLayoutView *activeView;
  MainMenuScreen *activeMenu, *errorMenu;
  int activeScreen, observedScreen;
  std::string disavowedMessage;
  PopupKind popupKind;
  unsigned awardBits;
  bool awardsInitialized, returnReady;
};

} // namespace HDUI

#endif
