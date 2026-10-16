#include "hd_screens.h"
#include "hd_intro.h"
#include "hd_allegro5.h"
#include "hd_atlas.h"
#include "hd_app_windows.h"
#include "hd_end_screens.h"
#include "hd_legacy_bridge.h"
#include "hd_local_windows.h"
#include "hd_map_lan.h"
#include "hd_remote_core.h"
#include "hd_remote_data.h"
#include "hd_remote_services.h"
#include "soundgarden.h"
#include "gucci.h"
#include "vanbakel.h"
#include "app/app.h"
#include "app/globals.h"
#include "app/opengl_interface.h"
#include "game/data/data.h"
#include "game/game.h"
#include "game/scriptlibrary.h"
#include "interface/interface.h"
#include "interface/localinterface/localinterface.h"
#include "interface/localinterface/hud_interface.h"
#include "interface/remoteinterface/remoteinterface.h"
#include "mainmenu/mainmenu.h"
#include "options/options.h"
#include "world/world.h"
#include "world/player.h"
#include "world/message.h"
#include "world/company/mission.h"
#include "world/vlocation.h"
#include "world/computer/bankaccount.h"
#include <cstdio>
#include <cstring>
#include <ctime>

namespace HDUI {
namespace {
std::string AppPath(const char *relative) {
  std::string path = app->path;
  if (!path.empty() && path.back() != '/' && path.back() != '\\')
    path += '/';
  return path + relative;
}

// IsRunning excludes speed zero. A paused session still owns a live HUD/model.
// The legacy HUD also distinguishes it from a not-yet-loaded empty Game.
bool HasGameSession() {
  return app && !app->Closed() && game && game->GameSpeed() >= 0 &&
         !app->GetMainMenu()->IsVisible() && EclGetButton("hud_mainmenu");
}

bool IsGlobalHotkeyCommand(const std::string &command) {
  return command.compare(0, 5, "Open ") == 0 ||
         command.compare(0, 14, "Set Gamespeed ") == 0 ||
         command == "View Notes" || command == "Options" ||
         command == "Log Out" || command == "Quit" ||
         command == "Disconnect" || command == "Save Game";
}

bool IsLogin() {
  return app && !app->Closed() &&
         app->GetMainMenu()->InScreen() == MAINMENU_LOGIN;
}

bool LegacyLocalOwnsWorkspace() {
  if (!HasGameSession())
    return false;
  const int screen = game->GetInterface()->GetLocalInterface()->InScreen();
  return screen == SCREEN_IRC || screen == SCREEN_EVTQUEUE;
}

bool ExistingProfile(const std::string &username, std::string *first = nullptr) {
  bool found = false;
  DArray<char *> *profiles = App::ListExistingGames();
  for (int i = 0; i < profiles->Size(); ++i) {
    if (!profiles->ValidIndex(i))
      continue;
    char *name = profiles->GetData(i);
    if (first && first->empty())
      *first = name;
    if (username == name)
      found = true;
    delete[] name;
  }
  delete profiles;
  return found;
}

int LatestEmail(Player *player) {
  int latest = -1, unread = -1;
  for (int i = 0; i < player->messages.Size(); ++i) {
    if (!player->messages.ValidIndex(i))
      continue;
    Message *message = player->messages.GetData(i);
    if (!message)
      continue;
    if (latest < 0 || message->date.After(&player->messages.GetData(latest)->date))
      latest = i;
    if (!message->IsRead() &&
        (unread < 0 || message->date.After(&player->messages.GetData(unread)->date)))
      unread = i;
  }
  return unread >= 0 ? unread : latest;
}

void RememberSignIn(Player *player) {
  char date[32] = "";
  const std::time_t now = std::time(nullptr);
  const std::tm *localTime = std::localtime(&now);
  if (localTime)
    std::strftime(date, sizeof(date), "%d.%m.%Y", localTime);
  Options *options = app->GetOptions();
  options->SetOptionValue("game_lastuser", 0, player->handle, false, false);
  options->SetOptionValue("game_lastuserdate", 0, date, false, false);
  options->Save(nullptr);
}

void Disable(UIObject *object) {
  if (object) {
    object->enabled = false;
    object->interactive = false;
    object->focusable = false;
  }
}

bool OpenInternalHelp() {
  if (!HasGameSession())
    return false;
  Player *player = game->GetWorld()->GetPlayer();
  Connection *connection = player->GetConnection();
  if (player->IsConnected())
    connection->Disconnect();
  connection->Reset();
  char internalServices[] = IP_UPLINKINTERNALSERVICES;
  connection->AddVLocation(internalServices);
  connection->Connect();

  RemoteInterface *remote = game->GetInterface()->GetRemoteInterface();
  remote->RunNewLocation();
  BTree<char *> *knownCode = player->codes.LookupTree(internalServices);
  if (knownCode && knownCode->data) {
    char username[256], password[256];
    if (Agent::ParseAccessCode(knownCode->data, username, sizeof(username),
                               password, sizeof(password))) {
      const int security = player->HasAccount(internalServices);
      if (security >= 0)
        remote->SetSecurity(username, security);
    }
  }
  remote->RunScreen(20);
  return true;
}
} // namespace

HDScreen::HDScreen(UIObject &root)
    : ownerRoot(root), layoutPtr(nullptr), visible(false) {}
HDScreen::~HDScreen() { Remove(); }

void HDScreen::Create() {
  Remove();
  if (Allegro5System::IsInitialized() && !layoutName.empty())
    visible = LoadLayout(layoutName);
}

void HDScreen::Remove() {
  visible = false;
  if (layoutPtr) {
    ownerRoot.DetachChild(layoutPtr);
    layoutPtr = nullptr;
  }
}

void HDScreen::Update(float dt) {
  if (IsVisible())
    layoutPtr->Update(dt);
}

void HDScreen::Draw() {
  if (IsVisible() && Allegro5System::IsInitialized())
    layoutPtr->Draw();
}

bool HDScreen::LoadLayout(const std::string &name) {
  std::unique_ptr<UIObject> layout = LayoutManager::GetInstance().TakeLayout(name);
  if (!layout) {
    std::fprintf(stderr, "HD_UI: Failed to load layout: %s\n", name.c_str());
    return false;
  }
  layoutPtr = layout.get();
  ownerRoot.AddChild(std::move(layout));
  return true;
}

UIObject *HDScreen::FindElement(const std::string &path) {
  return layoutPtr ? layoutPtr->FindChildRecursive(path) : nullptr;
}

void HDScreen::Bind(const char *path, const char *command) {
  UIObject *control = FindElement(path);
  if (control) {
    const std::string action(command);
    control->onClickCallback = [action](UIObject *) {
      HDUIManager::GetInstance().Invoke(action);
    };
  }
}

HD_MainMenu::HD_MainMenu(UIObject &root)
    : HDScreen(root), password(nullptr), shutdownPopup(nullptr),
      shutdownDismissKey(-1) {
  layoutName = "MainMenu/MainMenu";
}

void HD_MainMenu::Create() {
  HDScreen::Create();
  if (!IsVisible())
    return;
  // The full-screen menu tree owns otherwise-unhandled pointer events so they
  // cannot activate the hidden legacy login controls underneath it.
  layoutPtr->interactive = true;
  SetVersion("GOLD v1.04");
  Option *lastUser = app->GetOptions()->GetOption("game_lastuser");
  std::string username = lastUser ? lastUser->tooltip : "";
  if (username.empty())
    ExistingProfile("", &username);
  SetUsername(username.empty() ? "UNDEFINED" : username);
  Option *lastDate = app->GetOptions()->GetOption("game_lastuserdate");
  SetLastSignIn(lastDate ? lastDate->tooltip : "UNDEFINED");
  HideError();
  Bind("btnOptions", "Options");
  if (UIObject *shutdown = FindElement("btnShutdown"))
    shutdown->onClickCallback = [this](UIObject *) { ShowShutdownConfirmation(); };
  if (UIObject *submit = FindElement("signIn/btnSubmit"))
    submit->onClickCallback = [this](UIObject *) { Submit(); };
  password = dynamic_cast<UIButtonTextField *>(FindElement("signIn/btnPassword"));
  if (password) {
    password->isPassword = true;
    password->maxLength = 255;
    password->SetText("");
    password->onEnterCallback = [this](UIObject *) { Submit(); };
    ownerRoot.SetFocus(password);
  }
  std::unique_ptr<UIObject> modal = LayoutManager::GetInstance().TakeLayout("PopUp");
  if (modal) {
    shutdownPopup = modal.get();
    shutdownPopup->visible = false;
    shutdownPopup->interactive = true;
    shutdownPopup->zOrder = 1000;
    if (UITextPoint *title = dynamic_cast<UITextPoint *>(
            shutdownPopup->FindChildRecursive("dialogueBox/txtTitle")))
      title->text = "Shutdown";
    if (UITextParagraph *contents = dynamic_cast<UITextParagraph *>(
            shutdownPopup->FindChildRecursive("dialogueBox/txtContents")))
      contents->text = "Are you sure you want to shutdown Uplink?";
    if (UIObject *cancel =
            shutdownPopup->FindChildRecursive("dialogueBox/btnCancel"))
      cancel->onClickCallback = [this](UIObject *) { HideShutdownConfirmation(); };
    if (UIObject *confirm = shutdownPopup->FindChildRecursive("dialogueBox/btnOK"))
      confirm->onClickCallback = [](UIObject *) {
        HDUIManager::GetInstance().Invoke("Quit");
      };
    layoutPtr->AddChild(std::move(modal));
  }
}

void HD_MainMenu::Remove() {
  password = nullptr;
  shutdownPopup = nullptr;
  shutdownDismissKey = -1;
  HDScreen::Remove();
}

void HD_MainMenu::ShowShutdownConfirmation() {
  if (!shutdownPopup)
    return;
  shutdownPopup->visible = true;
  shutdownPopup->BringToFront();
  ownerRoot.SetFocus(
      shutdownPopup->FindChildRecursive("dialogueBox/btnCancel"));
}

void HD_MainMenu::HideShutdownConfirmation() {
  if (!shutdownPopup)
    return;
  shutdownPopup->visible = false;
  ownerRoot.SetFocus(password);
}

bool HD_MainMenu::IsShutdownConfirmationVisible() const {
  return shutdownPopup && shutdownPopup->IsVisible();
}

bool HD_MainMenu::HandleInput(const ALLEGRO_EVENT &event) {
  const bool keyboard = event.type == ALLEGRO_EVENT_KEY_DOWN ||
                        event.type == ALLEGRO_EVENT_KEY_CHAR ||
                        event.type == ALLEGRO_EVENT_KEY_UP;
  if (keyboard && shutdownDismissKey == event.keyboard.keycode) {
    if (event.type == ALLEGRO_EVENT_KEY_UP)
      shutdownDismissKey = -1;
    return true;
  }
  if (!IsShutdownConfirmationVisible())
    return false;
  if (keyboard) {
    if (event.type == ALLEGRO_EVENT_KEY_DOWN) {
      UIObject *cancel =
          shutdownPopup->FindChildRecursive("dialogueBox/btnCancel");
      UIObject *confirm =
          shutdownPopup->FindChildRecursive("dialogueBox/btnOK");
      const int key = event.keyboard.keycode;
      if (key == ALLEGRO_KEY_ESCAPE) {
        shutdownDismissKey = key;
        HideShutdownConfirmation();
      } else if (key == ALLEGRO_KEY_TAB && cancel && confirm) {
        UIObject *focused = ownerRoot.GetFocus();
        ownerRoot.SetFocus(focused == cancel ? confirm : cancel);
      } else if (key == ALLEGRO_KEY_ENTER ||
                 key == ALLEGRO_KEY_PAD_ENTER ||
                 key == ALLEGRO_KEY_SPACE) {
        shutdownDismissKey = key;
        if (cancel && ownerRoot.GetFocus() == cancel)
          HideShutdownConfirmation();
        else
          HDUIManager::GetInstance().Invoke("Quit");
      }
    }
    return true;
  }
  if (event.type == ALLEGRO_EVENT_MOUSE_AXES ||
      event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN ||
      event.type == ALLEGRO_EVENT_MOUSE_BUTTON_UP) {
    Allegro5System::DispatchEvent(event);
    return true;
  }
  return false;
}

void HD_MainMenu::SetUsername(const std::string &name) {
  if (UITextPoint *text = dynamic_cast<UITextPoint *>(FindElement("signIn/txtUsername")))
    text->text = name;
}
void HD_MainMenu::SetLastSignIn(const std::string &date) {
  if (UITextPoint *text = dynamic_cast<UITextPoint *>(FindElement("signIn/txtLastSignIn")))
    text->text = "Last Sign In: " + date;
}
void HD_MainMenu::SetVersion(const std::string &version) {
  if (UITextPoint *text = dynamic_cast<UITextPoint *>(FindElement("signIn/txtVersion")))
    text->text = version;
}
void HD_MainMenu::ShowError(const std::string &message) {
  if (UITextPoint *text = dynamic_cast<UITextPoint *>(FindElement("signIn/txtNotice"))) {
    text->text = message;
    text->visible = true;
  }
}

void HD_MainMenu::SelectProfile(const std::string &username,
                                const std::string &sentinelPassword) {
  SetUsername(username.empty() ? "UNDEFINED" : username);
  if (password) {
    password->SetText(sentinelPassword);
    if (!username.empty())
      ownerRoot.SetFocus(password);
  }
  HideError();
}
void HD_MainMenu::HideError() {
  if (UIObject *notice = FindElement("signIn/txtNotice"))
    notice->visible = false;
}

void HD_MainMenu::Submit() {
  if (!IsLogin() || !IsVisible())
    return;
  UITextPoint *username = dynamic_cast<UITextPoint *>(FindElement("signIn/txtUsername"));
  // GOLD sub_476BD0/sub_476E30 checks only a nonempty password and an exact
  // existing-profile name. Profile decoding/errors remain App::LoadGame's job.
  if (!password || password->inputText.empty() || !username ||
      !ExistingProfile(username->text)) {
    ShowError("Incorrect username or password");
    return;
  }
  app->SetNextLoadGame(username->text.c_str());
  app->GetMainMenu()->RunScreen(MAINMENU_LOADING);
  GciTimerFunc(1, ScriptLibrary::RunScript, 91);
}

HD_TopBar::HD_TopBar(UIObject &root)
    : HDScreen(root), dayText(nullptr), timeText(nullptr), accountText(nullptr),
      locationText(nullptr), ipText(nullptr), userText(nullptr), speedButtons{},
      speedMarker(nullptr), cpuFill(nullptr), cpuBase(nullptr) {
  layoutName = "HUD/TopBar";
}

void HD_TopBar::Create() {
  HDScreen::Create();
  if (!IsVisible())
    return;
  dayText = dynamic_cast<UITextPoint *>(FindElement("txtDay"));
  timeText = dynamic_cast<UITextPoint *>(FindElement("txtTime"));
  accountText = dynamic_cast<UITextPoint *>(FindElement("btnAccount/txtCaption"));
  locationText = dynamic_cast<UITextPoint *>(FindElement("txtLocName"));
  ipText = dynamic_cast<UITextPoint *>(FindElement("txtLocIP"));
  userText = dynamic_cast<UITextPoint *>(FindElement("btnUser/txtCaption"));
  speedMarker = FindElement("speedMarker");
  cpuFill = FindElement("btnCPU/fill");
  cpuBase = FindElement("btnCPU/fillBase");
  for (int i = 0; i < 4; ++i) {
    char id[16], command[32];
    std::snprintf(id, sizeof(id), "btnSpeed%d", i);
    std::snprintf(command, sizeof(command), "Set Gamespeed %d", i);
    speedButtons[i] = FindElement(id);
    Bind(id, command);
  }
  Bind("btnUser", "Open Status");
  Bind("btnAccount", "Open Finance");
  Bind("btnDisconnect", "Disconnect");
  Bind("btnSystem", "Open System Menu");
  Bind("btnCPU", "Open Task Manager");
  HDUIManager &manager = HDUIManager::GetInstance();
  SetTooltip(FindElement("btnUser"), "View status",
             manager.HotkeyHint("Open Status"));
  SetTooltip(FindElement("btnAccount"), "View finances",
             manager.HotkeyHint("Open Finance"));
  SetTooltip(FindElement("btnDisconnect"), "Disconnect or sign out");
  SetTooltip(FindElement("btnSystem"), "System menu");
  SetTooltip(FindElement("btnCPU"), "Task manager");
  for (int i = 0; i < 4; ++i) {
    char command[32];
    std::snprintf(command, sizeof(command), "Set Gamespeed %d", i);
    SetTooltip(speedButtons[i], i == 0 ? "Pause" : "Set game speed",
               manager.HotkeyHint(command));
  }
  Disable(FindElement("btnDate"));
  if (UIObject *notice = FindElement("btnAccount/noticeRed"))
    notice->visible = false;
  if (UIObject *notice = FindElement("btnAccount/noticeGreen"))
    notice->visible = false;
}

void HD_TopBar::SetDate(int day, int month, int year) {
  if (dayText) {
    char text[32];
    std::snprintf(text, sizeof(text), "%d-%d-%d", day, month, year);
    dayText->text = text;
  }
}
void HD_TopBar::SetTime(int hour, int minute) {
  if (timeText) {
    char text[16];
    std::snprintf(text, sizeof(text), "%02d:%02d", hour, minute);
    timeText->text = text;
  }
}
void HD_TopBar::SetSpeed(int speed) {
  if (speed >= 0 && speed < 4 && speedButtons[speed] && speedMarker)
    speedMarker->x = speedButtons[speed]->x;
}
void HD_TopBar::SetAccount(const char *accountNum, int balance) {
  if (accountText) {
    char text[128];
    if (accountNum && *accountNum)
      std::snprintf(text, sizeof(text), "Acc no. %s: %dc", accountNum, balance);
    else
      std::snprintf(text, sizeof(text), "No current account");
    accountText->text = text;
  }
}
void HD_TopBar::SetLocation(const char *name, const char *ip) {
  if (locationText)
    locationText->text = name;
  if (ipText)
    ipText->text = ip;
}
void HD_TopBar::SetCPU(float usage) {
  if (cpuFill && cpuBase) {
    if (usage < 0) usage = 0;
    if (usage > 1) usage = 1;
    cpuFill->w = cpuBase->w * usage;
  }
}
void HD_TopBar::SetUsername(const char *name) {
  if (userText)
    userText->text = name;
}

HD_TaskBar::HD_TaskBar(UIObject &root)
    : HDScreen(root), emailCount(nullptr), notification(nullptr) {
  layoutName = "HUD/TaskBar";
}
void HD_TaskBar::Create() {
  HDScreen::Create();
  if (!IsVisible())
    return;
  layoutPtr->y = 1080 - layoutPtr->h;
  emailCount = dynamic_cast<UITextPoint *>(FindElement("btnEmail/notification/txtNumber"));
  notification = FindElement("btnEmail/notification");
  Bind("btnApps", "Open App Menu");
  Bind("btnFiles", "Open Files Manager");
  Bind("btnLAN", "Open LAN View");
  Bind("btnEmail", "Open E-Mail");
  Bind("btnNotes", "View Notes");
  HDUIManager &manager = HDUIManager::GetInstance();
  SetTooltip(FindElement("btnApps"), "View apps",
             manager.HotkeyHint("Open App Menu"));
  SetTooltip(FindElement("btnFiles"), "View files",
             manager.HotkeyHint("Open Files Manager"));
  SetTooltip(FindElement("btnLAN"), "View LAN",
             manager.HotkeyHint("Open LAN View"));
  SetTooltip(FindElement("btnEmail"), "View e-mail",
             manager.HotkeyHint("Open E-Mail"));
  SetTooltip(FindElement("btnNotes"), "View notes",
             manager.HotkeyHint("View Notes"));
}
void HD_TaskBar::SetEmailCount(int count) {
  if (emailCount) {
    char text[16];
    std::snprintf(text, sizeof(text), "%d", count > 99 ? 99 : count);
    emailCount->text = text;
  }
  if (notification)
    notification->visible = count > 0;
}

HDUIManager &HDUIManager::GetInstance() {
  static HDUIManager instance;
  return instance;
}
HDUIManager::HDUIManager() : hdModeActive(false), lastFrameTime(0) {}
HDUIManager::~HDUIManager() { Shutdown(); }

bool HDUIManager::Initialize(int width, int height, bool fullscreen) {
  if (hdModeActive)
    return true;
  if (!app || !Allegro5System::Initialize(width, height, fullscreen))
    return false;
  LayoutManager &layouts = LayoutManager::GetInstance();
  layouts.SetLayoutDirectory(AppPath("uplinkHD/layouts"));
  layouts.SetAssetDirectory(AppPath("uplinkHD/graphics"));
  AtlasManager &atlas = AtlasManager::GetInstance();
  atlas.SetBasePath(AppPath("uplinkHD/graphics/"));
  if (!atlas.LoadAtlas("uplinkHD_atlas_00.xml") ||
      !layouts.GetLayout("MainMenu/MainMenu") ||
      !layouts.GetLayout("MainMenu/MMOptions") ||
      !layouts.GetLayout("IntroScreens") || !layouts.GetLayout("PopUp") ||
      !layouts.GetLayout("HUD/TopBar") || !layouts.GetLayout("HUD/TaskBar") ||
      !hotkeys.LoadDefaults(AppPath("uplinkHD/default_hotkeys.xml"))) {
    layouts.ClearCache();
    atlas.ClearAll();
    Allegro5System::Shutdown();
    return false;
  }
  Allegro5System::SetBaseResolution(1920, 1080);
  inputRoot.reset(new UIContainer());
  inputRoot->name = "HDOverlay";
  inputRoot->w = 1920;
  inputRoot->h = 1080;
  mainMenu.reset(new HD_MainMenu(*inputRoot));
  intro.reset(new HDIntro(*inputRoot));
  menuOptions.reset(new HDMMOptions(
      *inputRoot,
      [this](const std::string &username, const std::string &passwordValue) {
        mainMenu->SelectProfile(username, passwordValue);
      },
      [this] {
        menuOptions->Hide();
        app->GetOptions()->SetOptionValue("game_firsttime", 1);
        app->GetMainMenu()->RunScreen(MAINMENU_FIRSTLOAD);
        if (app->GetOptions()->IsOptionEqualTo("sound_musicenabled", 1))
          SgPlaylist_Play("main");
      },
      [this](const std::string &username) {
        const std::string notesPath =
            std::string(app->userpath) + username + "_notes.xml";
        const std::string hotkeysPath =
            std::string(app->userpath) + username + "_hotkeys.xml";
        notes->Discard(notesPath);
        hotkeys.DiscardUser(hotkeysPath);
        if (hotkeyUser == username)
          hotkeyUser.clear();
      }));
  topBar.reset(new HD_TopBar(*inputRoot));
  taskBar.reset(new HD_TaskBar(*inputRoot));
  notes.reset(new HDNotes(*inputRoot));
  taskManager.reset(new HDTaskManagerV2(*inputRoot));
  appManager.reset(new HDAppManager(*inputRoot));
  appManager->SetHotkeys(hotkeys);
  mapLan.reset(new HDMapLan(*inputRoot));
  localWindows.reset(new HDLocalWindows(*inputRoot));
  remoteCore.reset(new HDRemoteCore(*inputRoot));
  remoteData.reset(new HDRemoteData(*inputRoot));
  remoteServices.reset(new HDRemoteServices(*inputRoot));
  endScreens.reset(new HDEndScreens(*inputRoot));
  messageBoxes.reset(new HDLegacyMessageBoxMirror(*inputRoot));
  tooltips.reset(new HDTooltipController(*inputRoot));
  RegisterActions();
  hdModeActive = true;
  if (!GciRegisterInputObserver(ObserveInput, this)) {
    Shutdown();
    return false;
  }
  lastFrameTime = al_get_time();
  SynchronizeScreens();
  return true;
}

void HDUIManager::Shutdown() {
  GciUnregisterInputObserver(ObserveInput, this);
  if (!hdModeActive)
    return;
  hdModeActive = false;
  hotkeys.SaveIfChanged();
  hotkeyUser.clear();
  Allegro5System::SetInputRoot(nullptr);
  tooltips.reset();
  messageBoxes.reset();
  endScreens.reset();
  remoteServices.reset();
  remoteData.reset();
  remoteCore.reset();
  localWindows.reset();
  mapLan.reset();
  appManager.reset();
  notes.reset();
  taskManager.reset();
  intro.reset();
  menuOptions.reset();
  mainMenu.reset();
  topBar.reset();
  taskBar.reset();
  hotkeys.Clear();
  inputRoot.reset();
  LayoutManager::GetInstance().ClearCache();
  AtlasManager::GetInstance().ClearAll();
  Allegro5System::Shutdown();
}

void HDUIManager::RegisterActions() {
  hotkeys.RegisterAction("View Notes", [this] {
    if (!HasGameSession()) return false;
    if (notes->IsVisible()) {
      notes->Save();
      notes->Hide();
      return true;
    }
    const Player *player = game->GetWorld()->GetPlayer();
    return notes->Show(std::string(app->userpath) + player->handle + "_notes.xml");
  });
  hotkeys.RegisterAction("Open Task Manager", [this] {
    if (!HasGameSession()) return false;
    if (taskManager->IsVisible())
      taskManager->Hide();
    else if (!taskManager->Show())
      return false;
    return true;
  });
  hotkeys.RegisterAction("Quit", [this] {
    if (!app || app->Closed()) return false;
    if (HasGameSession()) {
      if (notes->IsVisible())
        notes->Save();
      hotkeys.SaveIfChanged();
      app->SaveGame(game->GetWorld()->GetPlayer()->handle);
    }
    app->Close();
    return true;
  });
  hotkeys.RegisterAction("Options", [this] {
    if (!IsLogin()) return false;
    if (menuOptions->IsVisible())
      menuOptions->Hide();
    else if (!menuOptions->Show())
      return false;
    return true;
  });
  hotkeys.RegisterAction("Log Out", [this] {
    if (!HasGameSession()) return false;
    localWindows->Hide();
    mapLan->Hide();
    appManager->Hide();
    HUDInterface::CloseGame();
    return true;
  });
  hotkeys.RegisterAction("Disconnect", [this] {
    if (!HasGameSession()) return false;
    Player *player = game->GetWorld()->GetPlayer();
    if (player->IsConnected()) {
      player->GetConnection()->Disconnect();
      player->GetConnection()->Reset();
      game->GetInterface()->GetRemoteInterface()->RunNewLocation();
      game->GetInterface()->GetRemoteInterface()->RunScreen(0);
      return true;
    }
    return hotkeys.Invoke("Log Out");
  });
  hotkeys.RegisterAction("Open Internal Help", [] {
    return OpenInternalHelp();
  });

  const char *commands[] = {
      "Open App Menu", "Open E-Mail", "Open Files Manager", "Open LAN View",
      "Open Status", "Open Finance", "Open Gateway Info", "Open System",
      "Open System Menu", "Open Options", "Show On Map", "Favourite",
      "Bookmark", "Copy", "Delete", "Decrypt", "Undo", "Modify", "Reply",
      "Play", "LAN Probe", "LAN Scan", "LAN Spoof", "LAN Force",
      "LAN Connect", "LAN Reset"};
  for (const char *command : commands) {
    const std::string action(command);
    hotkeys.RegisterAction(action, [this, action] {
      return HasGameSession() && InvokeGameCommand(action);
    });
  }
  appManager->RegisterHotkeyActions(hotkeys);
  for (int speed = 0; speed < 4; ++speed) {
    char command[32];
    std::snprintf(command, sizeof(command), "Set Gamespeed %d", speed);
    hotkeys.RegisterAction(command, [speed] {
      if (!HasGameSession()) return false;
      game->SetGameSpeed(speed);
      for (int i = 0; i < 4; ++i) {
        char button[32];
        std::snprintf(button, sizeof(button), "hud_speed %d", i);
        EclDirtyButton(button);
      }
      return true;
    });
  }
}

std::string HDUIManager::HotkeyHint(const std::string &command) const {
  for (const HDHotkeyManager::Binding &binding : hotkeys.GetBindings()) {
    if (binding.command != command)
      continue;
    std::string hint;
    if (binding.modifiers & ALLEGRO_KEYMOD_CTRL) hint += "Ctrl+";
    if (binding.modifiers & ALLEGRO_KEYMOD_ALT) hint += "Alt+";
    if (binding.modifiers & ALLEGRO_KEYMOD_SHIFT) hint += "Shift+";
    const char *name = al_keycode_to_name(binding.key);
    hint += name && *name ? name : std::to_string(binding.key);
    return hint;
  }
  return std::string();
}

bool HDUIManager::InvokeGameCommand(const std::string &command) {
  if (mapLan && mapLan->Invoke(command)) return true;
  if (appManager && appManager->Invoke(command)) return true;
  if (localWindows && localWindows->Invoke(command)) return true;
  if (remoteData && remoteData->Invoke(command)) return true;
  if (remoteServices && remoteServices->Invoke(command)) return true;
  if (remoteCore && remoteCore->Invoke(command)) return true;
  if (endScreens && endScreens->Invoke(command)) return true;
  return false;
}

bool HDUIManager::InvokeContextCommand(const std::string &command) {
  if (!hdModeActive)
    return false;
  if (mapLan && mapLan->Invoke(command)) return true;
  if (localWindows && localWindows->Invoke(command)) return true;
  if (remoteData && remoteData->Invoke(command)) return true;
  if (remoteServices && remoteServices->Invoke(command)) return true;
  if (remoteCore && remoteCore->Invoke(command)) return true;
  if (endScreens && endScreens->Invoke(command)) return true;
  return false;
}

bool HDUIManager::Invoke(const std::string &command) {
  if (!hdModeActive)
    return false;
  if (hotkeys.Invoke(command))
    return true;
  return InvokeGameCommand(command);
}

void HDUIManager::UpdateInputRoot() {
  const bool visible =
      intro->IsVisible() || mainMenu->IsVisible() || topBar->IsVisible() ||
      taskBar->IsVisible() || notes->IsVisible() || taskManager->IsVisible() ||
      appManager->IsVisible() || mapLan->IsVisible() ||
      localWindows->IsVisible() || remoteCore->IsVisible() ||
      remoteData->IsVisible() || remoteServices->IsVisible() ||
      endScreens->IsVisible() || messageBoxes->IsVisible();
  Allegro5System::SetInputRoot(visible ? inputRoot.get() : nullptr);
}

void HDUIManager::SynchronizeScreens() {
  if (!hdModeActive)
    return;
  messageBoxes->Update();
  const bool firstLoad = app && !app->Closed() &&
                         app->GetMainMenu()->InScreen() == MAINMENU_FIRSTLOAD;
  if (firstLoad) {
    if (!intro->IsVisible())
      intro->Create();
  } else if (intro->IsVisible()) {
    intro->Remove();
  }
  const bool login = !firstLoad && IsLogin();
  const bool legacyDialog = login && isvisible_msgbox();
  const bool session = !firstLoad && HasGameSession();
  if (!login || legacyDialog)
    menuOptions->Hide();
  if (login && !legacyDialog) {
    if (!mainMenu->IsVisible())
      mainMenu->Create();
  } else {
    mainMenu->Remove();
  }
  if (session) {
    if (!topBar->IsVisible()) {
      topBar->Create();
      taskBar->Create();
      Player *player = game->GetWorld()->GetPlayer();
      RememberSignIn(player);
    }
  } else {
    topBar->Remove();
    taskBar->Remove();
    if (notes->IsVisible()) {
      notes->Save();
      notes->Hide();
    }
    taskManager->Hide();
    appManager->Hide();
    mapLan->Hide();
    localWindows->Hide();
    remoteCore->Hide();
    remoteData->Hide();
    remoteServices->Hide();
  }
  const char *user = "";
  if (session)
    user = game->GetWorld()->GetPlayer()->handle;
  else if (login && !legacyDialog) {
    Option *lastUser = app->GetOptions()->GetOption("game_lastuser");
    if (lastUser) user = lastUser->tooltip;
  }
  if (hotkeyUser != user) {
    hotkeys.SaveIfChanged();
    hotkeyUser = user;
    hotkeys.LoadUser(hotkeyUser.empty() ? "" :
                    std::string(app->userpath) + hotkeyUser + "_hotkeys.xml");
  }
  UpdateInputRoot();
}

void HDUIManager::UpdateHUD() {
  if (!HasGameSession() || !topBar->IsVisible())
    return;
  World *world = game->GetWorld();
  Player *player = world->GetPlayer();
  topBar->SetDate(world->date.GetDay(), world->date.GetMonth(), world->date.GetYear());
  topBar->SetTime(world->date.GetHour(), world->date.GetMinute());
  topBar->SetUsername(player->handle);
  topBar->SetSpeed(game->GameSpeed());
  VLocation *location = world->GetVLocation(player->remotehost);
  topBar->SetLocation(location ? location->computer : player->remotehost, player->remotehost);
  BankAccount *account = nullptr;
  char bankIP[SIZE_VLOCATION_IP], number[64];
  if (player->accounts.ValidIndex(player->currentaccount) &&
      std::sscanf(player->accounts.GetData(player->currentaccount), "%23s %63s", bankIP, number) == 2)
    account = BankAccount::GetAccount(bankIP, number);
  topBar->SetAccount(account ? number : "", account ? account->balance : 0);
  double cpu = 0;
  for (int i = 0; i < SvbNumTasks(); ++i) {
    TaskWrapper *task = SvbGetTaskWrapperAtIndex(i);
    if (task) cpu += task->priority;
  }
  topBar->SetCPU(static_cast<float>(cpu));
  int unread = 0;
  for (int i = 0; i < player->messages.Size(); ++i) {
    if (player->messages.ValidIndex(i)) {
      Message *message = player->messages.GetData(i);
      if (message && !message->IsRead()) ++unread;
    }
  }
  for (int i = 0; i < player->missions.Size(); ++i) {
    if (player->missions.ValidIndex(i)) {
      Mission *mission = player->missions.GetData(i);
      if (mission && !mission->IsRead()) ++unread;
    }
  }
  taskBar->SetEmailCount(unread);
}

bool HDUIManager::ObserveInput(const ALLEGRO_EVENT &event, void *context) {
  return static_cast<HDUIManager *>(context)->HandleInput(event);
}
bool HDUIManager::HandleInput(const ALLEGRO_EVENT &event) {
  if (!hdModeActive)
    return false;
  ALLEGRO_DISPLAY *display = Allegro5System::GetDisplay();
  Allegro5System::SetDisplaySize(al_get_display_width(display),
                                 al_get_display_height(display));
  SynchronizeScreens();
  const bool legacyLocalWorkspace = LegacyLocalOwnsWorkspace();
  if (legacyLocalWorkspace) {
    remoteCore->Hide();
    remoteData->Hide();
    remoteServices->Hide();
    inputRoot->SetFocus(nullptr);
  }
  messageBoxes->Update();
  const bool keyboard = event.type == ALLEGRO_EVENT_KEY_DOWN ||
                        event.type == ALLEGRO_EVENT_KEY_CHAR ||
                        event.type == ALLEGRO_EVENT_KEY_UP;

  bool consumed = messageBoxes->HandleInput(event);
  if (!consumed) consumed = endScreens->HandleInput(event);
  if (!consumed) consumed = mainMenu->HandleInput(event);
  if (!consumed) consumed = menuOptions->HandleInput(event);
  if (!consumed) consumed = localWindows->HandleInput(event);
  if (!consumed) consumed = appManager->HandleInput(event);
  if (!consumed) consumed = mapLan->HandleInput(event);
  if (!consumed && !legacyLocalWorkspace)
    consumed = remoteServices->HandleModalInput(event);
  if (!consumed && !legacyLocalWorkspace)
    consumed = remoteData->HandleModalInput(event);
  if (!consumed && keyboard)
    consumed = hotkeys.DispatchEvent(event, IsGlobalHotkeyCommand);
  if (!consumed && !legacyLocalWorkspace)
    consumed = remoteServices->HandleInput(event);
  if (!consumed && !legacyLocalWorkspace)
    consumed = remoteData->HandleInput(event);
  if (!consumed && !legacyLocalWorkspace)
    consumed = remoteCore->HandleInput(event);
  if (!consumed) consumed = intro->HandleInput(event);
  UIObject *focused = inputRoot->GetFocus();
  const bool editableFocus =
      keyboard && dynamic_cast<UIButtonTextField *>(focused);
  char *legacyFocus = legacyLocalWorkspace ? EclGetHighlightedButton() : nullptr;
  const bool legacyEditableFocus =
      keyboard && legacyFocus && EclIsButtonEditable(legacyFocus);
  if (!consumed && editableFocus) {
    Allegro5System::DispatchEvent(event);
    consumed = true;
  }
  if (!consumed && !legacyEditableFocus)
    consumed = hotkeys.DispatchEvent(event);
  const bool introCaptures = intro->IsVisible();
  const bool surfaceCaptures =
      introCaptures || mainMenu->IsVisible() || menuOptions->IsVisible() ||
      endScreens->IsVisible() || mapLan->CapturesKeyboard() ||
      remoteServices->IsVisible() || remoteData->IsVisible() ||
      remoteCore->IsVisible() || messageBoxes->IsVisible();
  if (!consumed && (!keyboard || surfaceCaptures || inputRoot->GetFocus()))
    consumed = Allegro5System::DispatchEvent(event);
  if (event.type == ALLEGRO_EVENT_KEY_DOWN && inputRoot->GetFocus() &&
      (event.keyboard.keycode == ALLEGRO_KEY_ENTER ||
       event.keyboard.keycode == ALLEGRO_KEY_PAD_ENTER ||
       event.keyboard.keycode == ALLEGRO_KEY_SPACE ||
       event.keyboard.keycode == ALLEGRO_KEY_TAB))
    consumed = true;
  if (surfaceCaptures && keyboard)
    consumed = true;
  if (introCaptures && (event.type == ALLEGRO_EVENT_MOUSE_AXES ||
                        event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN ||
                        event.type == ALLEGRO_EVENT_MOUSE_BUTTON_UP))
    consumed = true;
  if (event.type == ALLEGRO_EVENT_KEY_UP)
    hotkeys.ReleaseConsumedKey(event.keyboard.keycode);

  messageBoxes->Update();
  SynchronizeScreens();
  return consumed;
}

void HDUIManager::Update(float dt) {
  if (!hdModeActive)
    return;
  ALLEGRO_DISPLAY *display = Allegro5System::GetDisplay();
  Allegro5System::SetDisplaySize(al_get_display_width(display),
                                 al_get_display_height(display));
  SynchronizeScreens();
  endScreens->Update();
  messageBoxes->Update();
  if (intro->IsVisible()) {
    intro->Update(dt);
    if (app->GetMainMenu()->InScreen() != MAINMENU_FIRSTLOAD)
      SynchronizeScreens();
  }
  if (HasGameSession()) {
    appManager->Update();
    mapLan->Update();
    localWindows->Update();
    if (LegacyLocalOwnsWorkspace()) {
      remoteCore->Hide();
      remoteData->Hide();
      remoteServices->Hide();
    } else {
      remoteCore->Update();
      remoteData->Update();
      remoteServices->Update();
    }
    taskManager->Update();
  }

  UpdateHUD();
  inputRoot->Update(dt);
  tooltips->Update(dt);
  UpdateInputRoot();
}
void HDUIManager::Draw() {
  if (!hdModeActive || !Allegro5System::IsInitialized())
    return;
  const double now = al_get_time();
  Update(static_cast<float>(now - lastFrameTime));
  lastFrameTime = now;
  if (!Allegro5System::GetInputRoot())
    return;
  Allegro5System::BeginFrame();
  inputRoot->Draw();
}

} // namespace HDUI
