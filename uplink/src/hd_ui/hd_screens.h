#ifndef _included_hd_screens_h
#define _included_hd_screens_h

#include "hd_layout_parser.h"
#include "hd_hotkeys.h"
#include "hd_mm_options.h"
#include "hd_notes.h"
#include "hd_taskmanager_v2.h"
#include <memory>
#include <string>

namespace HDUI {

class HDIntro;
class HDAppManager;
class HDEndScreens;
class HDLegacyMessageBoxMirror;
class HDTooltipController;
class HDLocalWindows;
class HDMapLan;
class HDRemoteCore;
class HDRemoteData;
class HDRemoteServices;

// Screen trees are owned by the manager's single composed input root.
class HDScreen {
public:
  explicit HDScreen(UIObject &root);
  virtual ~HDScreen();
  virtual void Create();
  virtual void Remove();
  virtual void Update(float dt);
  virtual void Draw();
  bool IsVisible() const { return visible && layoutPtr; }

protected:
  UIObject &ownerRoot;
  UIObject *layoutPtr;
  bool visible;
  std::string layoutName;
  bool LoadLayout(const std::string &name);
  UIObject *FindElement(const std::string &path);
  void Bind(const char *path, const char *command);
};

class HD_MainMenu : public HDScreen {
public:
  explicit HD_MainMenu(UIObject &root);
  void Create() override;
  void Remove() override;
  void SetUsername(const std::string &name);
  void SetLastSignIn(const std::string &date);
  void SetVersion(const std::string &version);
  void ShowError(const std::string &message);
  void HideError();
  void Submit();
  void SelectProfile(const std::string &username, const std::string &sentinelPassword);
  void ShowShutdownConfirmation();
  void HideShutdownConfirmation();
  bool HandleInput(const ALLEGRO_EVENT &event);
  bool IsShutdownConfirmationVisible() const;

private:
  UIButtonTextField *password;
  UIObject *shutdownPopup;
  int shutdownDismissKey;
};

class HD_TopBar : public HDScreen {
public:
  explicit HD_TopBar(UIObject &root);
  void Create() override;
  void SetDate(int day, int month, int year);
  void SetTime(int hour, int minute);
  void SetSpeed(int speed);
  void SetAccount(const char *accountNum, int balance);
  void SetLocation(const char *name, const char *ip);
  void SetCPU(float usage);
  void SetUsername(const char *name);

private:
  UITextPoint *dayText, *timeText, *accountText, *locationText, *ipText, *userText;
  UIObject *speedButtons[4];
  UIObject *speedMarker, *cpuFill, *cpuBase;
};

class HD_TaskBar : public HDScreen {
public:
  explicit HD_TaskBar(UIObject &root);
  void Create() override;
  void SetEmailCount(int count);

private:
  UITextPoint *emailCount;
  UIObject *notification;
};

// Eclipse remains the renderer and behavior backend for unmigrated screens.
class HDUIManager {
public:
  static HDUIManager &GetInstance();
  bool Initialize(int width, int height, bool fullscreen);
  void Shutdown();
  bool IsHDModeActive() const { return hdModeActive; }
  HD_MainMenu *GetMainMenu() { return mainMenu.get(); }
  HD_TopBar *GetTopBar() { return topBar.get(); }
  HD_TaskBar *GetTaskBar() { return taskBar.get(); }
  HDNotes *GetNotes() { return notes.get(); }
  HDTaskManagerV2 *GetTaskManager() { return taskManager.get(); }
  void Update(float dt);
  void Draw();
  bool Invoke(const std::string &command);
  bool InvokeContextCommand(const std::string &command);
  std::string HotkeyHint(const std::string &command) const;

private:
  HDUIManager();
  ~HDUIManager();
  static bool ObserveInput(const ALLEGRO_EVENT &event, void *context);
  bool HandleInput(const ALLEGRO_EVENT &event);
  void SynchronizeScreens();
  void UpdateHUD();
  void RegisterActions();
  bool InvokeGameCommand(const std::string &command);
  void UpdateInputRoot();
  bool hdModeActive;
  double lastFrameTime;
  std::string hotkeyUser;
  std::unique_ptr<UIContainer> inputRoot;
  std::unique_ptr<HD_MainMenu> mainMenu;
  std::unique_ptr<HDIntro> intro;
  std::unique_ptr<HDMMOptions> menuOptions;
  std::unique_ptr<HD_TopBar> topBar;
  std::unique_ptr<HD_TaskBar> taskBar;
  std::unique_ptr<HDNotes> notes;
  std::unique_ptr<HDTaskManagerV2> taskManager;
  std::unique_ptr<HDAppManager> appManager;
  std::unique_ptr<HDMapLan> mapLan;
  std::unique_ptr<HDLocalWindows> localWindows;
  std::unique_ptr<HDRemoteCore> remoteCore;
  std::unique_ptr<HDRemoteData> remoteData;
  std::unique_ptr<HDRemoteServices> remoteServices;
  std::unique_ptr<HDEndScreens> endScreens;
  std::unique_ptr<HDLegacyMessageBoxMirror> messageBoxes;
  std::unique_ptr<HDTooltipController> tooltips;
  HDHotkeyManager hotkeys;
};

} // namespace HDUI
#endif
