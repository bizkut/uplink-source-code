#include "hd_local_windows.h"
#include "hd_legacy_bridge.h"
#include "hd_layout_parser.h"
#include "hd_app_windows.h"
#include "hd_screens.h"

#include "app/app.h"
#include "app/globals.h"
#include "mainmenu/mainmenu.h"
#include "game/game.h"
#include "game/data/data.h"
#include "interface/interface.h"
#include "interface/localinterface/localinterface.h"
#include "interface/localinterface/localinterfacescreen.h"
#include "interface/localinterface/sendmail_interface.h"
#include "interface/remoteinterface/remoteinterface.h"
#include "interface/remoteinterface/remoteinterfacescreen.h"
#include "interface/taskmanager/taskmanager.h"
#include "interface/taskmanager/tutorial.h"
#include "interface/taskmanager/filecopier.h"
#include "interface/taskmanager/filedeleter.h"
#include "interface/taskmanager/decrypter.h"
#include "interface/taskmanager/uplinktask.h"
#include "interface/taskmanager/voiceanalyser.h"
#include "options/options.h"
#include "world/world.h"
#include "world/player.h"
#include "world/message.h"
#include "world/company/companyuplink.h"
#include "world/company/mission.h"
#include "world/company/sale.h"
#include "world/computer/bankaccount.h"
#include "world/computer/computer.h"
#include "world/computer/computerscreen/computerscreen.h"
#include "world/computer/recordbank.h"
#include "world/computer/securitysystem.h"
#include "world/generator/recordgenerator.h"
#include "eclipse.h"
#include "gucci.h"
#include "soundgarden.h"
#include "vanbakel.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

namespace HDUI {
namespace {
UIObject *Child(UIObject *root, const std::string &path) {
  return root ? root->FindChildRecursive(path) : nullptr;
}
void Text(UIObject *root, const std::string &path, const std::string &value) {
  if (UIObject *object = Child(root, path))
    SetText(object, value);
}
void Visible(UIObject *root, const std::string &path, bool value) {
  SetVisible(Child(root, path), value);
}
void Image(UIObject *root, const std::string &path, const std::string &file) {
  if (auto *image = dynamic_cast<UIImage *>(Child(root, path))) {
    if (image->file != file) {
      image->file = file;
      image->LoadBitmap("uplinkHD/graphics");
    }
  }
}
std::string Number(int value, const char *suffix = "") {
  return std::to_string(value) + suffix;
}
std::string Version(float version) {
  char text[32];
  std::snprintf(text, sizeof(text), "v%.1f", version);
  return text;
}
std::string Key(const void *object) {
  char text[48];
  std::snprintf(text, sizeof(text), "%p", object);
  return text;
}
std::string DisplayName(const std::string &name) {
  std::string result = name;
  std::replace(result.begin(), result.end(), '_', ' ');
  return result;
}
int HUDUpgrade(const std::string &name) {
  if (name == "HUD_MapShowTrace") return HUDUPGRADE_MAPSHOWSTRACE;
  if (name == "HUD_ConnectionAnalysis") return HUDUPGRADE_CONNECTIONANALYSIS;
  if (name == "HUD_IRC-Client") return HUDUPGRADE_IRCCLIENT;
  if (name == "HUD_LANView") return HUDUPGRADE_LANVIEW;
  return 0;
}
const char *AppIcon(const std::string &name) {
  static const std::pair<const char *, const char *> icons[] = {
      {"Password_Breaker", "passwordBreaker"}, {"Dictionary_Hacker", "dictionaryHacker"},
      {"Trace_Tracker", "traceTracker"}, {"Decrypter", "decrypter"},
      {"Log_Deleter", "logDeleter"}, {"Log_UnDeleter", "logUnDeleter"},
      {"Log_Modifier", "logModifier"}, {"IP_Lookup", "ipLookup"},
      {"IP_Probe", "ipProbe"}, {"Proxy_Disable", "proxyDisable"},
      {"Firewall_Disable", "firewallDisable"}, {"Proxy_Bypass", "proxyBypass"},
      {"Firewall_Bypass", "firewallBypass"}, {"Monitor_Bypass", "monitorBypass"},
      {"Gateway_Nuke", "gatewayNuke"}, {"Motion_Sensor", "motionSensor"},
      {"Decypher", "decypher"}, {"Voice_Analyser", "voiceAnalyser"},
      {"Revelation", "revelation"}, {"Revelation_Tracker", "revelationTracker"},
      {"Faith", "faith"}, {"Uplink_Agent_List", "uplinkAgentList"},
      {"LAN_Scan", "lanScan"}, {"LAN_Probe", "lanProbe"},
      {"LAN_Spoof", "lanSpoof"}, {"LAN_Force", "lanForce"},
      {"HUD_ConnectionAnalysis", "hudConnectionAnalysis"},
      {"HUD_MapShowTrace", "hudMapShowTrace"}, {"HUD_IRC-Client", "hudIRCClient"},
      {"HUD_LANView", "hudLANView"}};
  for (const auto &icon : icons)
    if (name == icon.first) return icon.second;
  return "unknown";
}

// Reconcile only from Update, never from one of the row callbacks. Model
// pointers are identities, not dereferenceable captures: callbacks re-resolve.
struct Rows {
  struct Row { UIObject *object; bool live; };
  std::map<std::string, Row> objects;
  void Begin() { for (auto &entry : objects) entry.second.live = false; }
  UIObject *Get(UIObject *parent, const std::string &key,
                HDRepeatLayoutFactory &factory) {
    if (!parent) return nullptr;
    auto found = objects.find(key);
    if (found != objects.end()) {
      found->second.live = true;
      return found->second.object;
    }
    std::unique_ptr<UIObject> row = factory.Create();
    if (!row) return nullptr;
    row->name = key;
    UIObject *result = row.get();
    parent->AddChild(std::move(row));
    objects.emplace(key, Row{result, true});
    return result;
  }
  void End() {
    for (auto row = objects.begin(); row != objects.end();) {
      if (!row->second.live) {
        UIObject *object = row->second.object;
        if (object->parent) object->parent->DetachChild(object);
        row = objects.erase(row);
      } else ++row;
    }
  }
};

// Retain the ordinary local interface while borrowing its real controls. A
// callback may replace its own screen; no borrowed Button survives that call.
void WithLegacyScreen(int screen, int index, const std::function<void()> &action) {
  LocalInterface *localInterface = game->GetInterface()->GetLocalInterface();
  int previous = localInterface->InScreen();
  int previousIndex = localInterface->screenindex;
  if (previous == screen && previousIndex == index) {
    action();
    return;
  }
  Player *player = game->GetWorld()->GetPlayer();
  Message *previousMessage = previous == SCREEN_EMAIL && player->messages.ValidIndex(previousIndex)
      ? player->messages.GetData(previousIndex) : nullptr;
  Mission *previousMission = previous == SCREEN_MISSION && player->missions.ValidIndex(previousIndex)
      ? player->missions.GetData(previousIndex) : nullptr;
  localInterface->RunScreen(screen, index);
  localInterface->Update();
  action();
  if (previous == SCREEN_EMAIL || previous == SCREEN_MISSION) {
    previousIndex = -1;
    if (previousMessage)
      for (int i = 0; i < player->messages.Size(); ++i)
        if (player->messages.GetData(i) == previousMessage) { previousIndex = i; break; }
    if (previousMission)
      for (int i = 0; i < player->missions.Size(); ++i)
        if (player->missions.GetData(i) == previousMission) { previousIndex = i; break; }
    if (previousIndex < 0) previous = SCREEN_NONE;
  }
  localInterface->RunScreen(previous, previousIndex);
}

struct HiddenButton {
  int x, y;
};
} // namespace

struct HDLocalWindows::Impl {
  enum Window { Apps, Email, Files, OptionsWindow, System, User, Tour, Analyser,
                Cheats, GatewayView, WindowCount };
  enum FileCommand { UploadFile, DecryptFile, PlayFile, RemoveFile, CancelFile };
  UIObject &owner;
  std::array<std::unique_ptr<HDLayoutView>, WindowCount> views;
  HDPopupController popup;
  HDLayoutView fileMenu; // Shared modal host for file actions and the system menu.
  HDRepeatLayoutFactory fileMenuFactory;
  HDRepeatLayoutFactory appFactory, emailFactory, fileFactory, accountFactory;
  HDRepeatLayoutFactory categoryFactory, separatorFactory, componentFactory;
  Rows appRows, emailRows, fileRows, accountRows, gatewayRows;
  int mailTab = 0, fileTab = 0, userTab = 0, systemTab = 0;
  Message *message = nullptr;
  Mission *mission = nullptr;
  Data *attachment = nullptr; // Borrowed from the legacy draft, which owns a copy.
  bool selectingAttachment = false, tourMinimized = false;
  struct Upload { int pid; ComputerScreen *destination; Data *file; };
  std::vector<Upload> uploads;
  std::vector<int> fileOperations;
  Data *contextFile = nullptr;
  float pointerX = -1, pointerY = -1;
  bool systemMenuOpen = false;
  int menuDismissKey = -1;
  int tutorialPID = -1;
  bool tourSuppressed = false;
  std::map<std::string, HiddenButton> hiddenTutorial;
  std::map<std::string, float> appVersions;
  std::map<int, UIObject *> categoryRoots;
  std::vector<std::pair<int, int>> resolutions;
  std::size_t resolution = 0;
  Player *playerIdentity = nullptr;
  std::string gatewaySignature;
  std::vector<Message *> mailboxSnapshot, orderedMessages;

  explicit Impl(UIObject &root)
      : owner(root), popup(root), fileMenu(root, "Misc/ButtonsMenuReference"),
        fileMenuFactory("Misc/ButtonsMenuReference"), appFactory("HUD/AppElement"),
        emailFactory("HUD/windows/WinEmailElement"),
        fileFactory("HUD/windows/WinFileElement"),
        accountFactory("HUD/windows/WinAccountElement"),
        categoryFactory("HUD/AppMenu", "txtCategTitleRef"),
        separatorFactory("HUD/AppMenu", "categTitleSepRef"),
        componentFactory("HUD/ViewGateway", "txtCompRef") {
    const char *layouts[] = {"HUD/AppMenu", "HUD/windows/WinEmail",
      "HUD/windows/WinFilesManager", "HUD/windows/WinOptions",
      "HUD/windows/WinSystem", "HUD/windows/WinUser",
      "HUD/windows/WinTourGuide", "HUD/windows/WinAnalyser",
      "HUD/windows/WinCheats", "HUD/ViewGateway"};
    for (int i = 0; i < WindowCount; ++i)
      views[i].reset(new HDLayoutView(root, layouts[i]));
    views[GatewayView]->SetCaptureInput(true);
  }

  Player *PlayerModel() const {
    return app && !app->Closed() && game && game->GameSpeed() >= GAMESPEED_PAUSED &&
           !app->GetMainMenu()->IsVisible() && EclGetButton("hud_mainmenu")
        ? game->GetWorld()->GetPlayer() : nullptr;
  }
  HDLayoutView &View(Window window) { return *views[window]; }
  UIObject *Root(Window window) { return View(window).Root(); }
  void Bind(Window window, const std::string &path, std::function<void()> action) {
    if (UIObject *control = View(window).Find(path)) {
      control->onClickCallback = [this, window, action](UIObject *) {
        View(window).BringToFront();
        SyncLegacyWindow(window);
        action();
      };
    }
  }
  void Tab(Window window, int tab) {
    UIObject *root = Root(window);
    UIObject *button = Child(root, "btnTab" + Number(tab));
    UIObject *selection = Child(root, "tabSelection");
    if (button && selection) {
      selection->x = button->x;
      selection->w = button->w;
      if (auto *line = dynamic_cast<UILine *>(selection)) line->x2 = button->w;
    }
  }
  bool Prepare(Window window) {
    if (Root(window)) return true;
    if (!View(window).Load()) return false;
    UIObject *root = Root(window);
    root->interactive = true;
    root->onClickCallback = [this, window](UIObject *) {
      View(window).BringToFront();
      SyncLegacyWindow(window);
    };
    const float positions[][2] = {{0,85},{1425,120},{925,120},{800,220},
      {1415,230},{415,230},{700,640},{640,800},{740,260},{0,0}};
    root->x = positions[window][0];
    root->y = positions[window][1];
    if (window != Apps && window != GatewayView) {
      const float height = window == Tour || window == Analyser ? 29.0f : 45.0f;
      const float width = root->w - (window == Tour ? 42.0f : 45.0f);
      root->AddChild(MakeDraggableTitle(0, 0, width, height));
    }
    if (window != Apps && window != Analyser)
      Bind(window, window == GatewayView ? "controls/btnClose" : "btnClose",
           [this, window] { Close(window); });
    switch (window) {
    case Apps:
      Visible(root, "txtCategTitleRef", false);
      Visible(root, "categTitleSepRef", false);
      break;
    case Email:
      for (int i = 0; i < 3; ++i)
        Bind(window, "btnTab" + Number(i), [this, i] {
          if (mailTab == 2) StoreDraft();
          mailTab = i; message = nullptr; mission = nullptr;
          SyncLegacyWindow(Email);
          if (i == 2) LoadDraft();
        });
      Bind(window, "btnPrevMessage", [this] { NavigateMail(-1); });
      Bind(window, "btnNextMessage", [this] { NavigateMail(1); });
      Bind(window, "viewMessage/btnAttachment", [this] { SaveAttachment(); });
      Bind(window, "viewMessage/btnReply", [this] { Reply(); });
      Bind(window, "viewMessage/btnDelete", [this] { DeleteMail(); });
      Bind(window, "composeMessage/btnAttach", [this] {
        selectingAttachment = !selectingAttachment;
        if (selectingAttachment) { Show(Files); fileTab = 1; }
      });
      Bind(window, "composeMessage/btnSend", [this] { Send(); });
      Bind(window, "composeMessage/btnCancel", [this] { CancelDraft(); });
      if (auto *to = View(window).FindAs<UIButtonTextField>("composeMessage/btnTo"))
        to->maxLength = SIZE_PERSON_NAME - 1;
      if (UIObject *contents = Child(root, "viewMessage/txtContents")) {
        std::unique_ptr<UIObject> text = contents->parent->DetachChild(contents);
        text->x = 0; text->y = 0;
        if (UIObject *scroll = Child(root, "viewMessage/contentsScroll"))
          scroll->AddChild(std::move(text));
      }
      break;
    case Files:
      for (int i = 0; i < 3; ++i)
        Bind(window, "btnTab" + Number(i), [this, i] { fileTab = i; });
      Bind(window, "hddSpace", [this] { TargetFreeSpace(); });
      if (UIObject *space = Child(root, "hddSpace")) space->interactive = true;
      Bind(window, "list", [this] { TargetFreeSpace(); });
      break;
    case OptionsWindow:
      Bind(window, "btnResolution", [this] {
        if (!resolutions.empty()) {
          resolution = (resolution + 1) % resolutions.size();
          ResolutionCaption();
        }
      });
      Bind(window, "btnApply", [this] { ApplyOptions(); });
      break;
    case System:
      Bind(window, "btnTab0", [this] { systemTab = 0; });
      Bind(window, "btnTab1", [this] { systemTab = 1; });
      Bind(window, "gateway/btnViewGateway", [this] { Show(GatewayView); });
      break;
    case User:
      Bind(window, "btnTab0", [this] { userTab = 0; SyncLegacyWindow(User); });
      Bind(window, "btnTab1", [this] { userTab = 1; SyncLegacyWindow(User); });
      for (int i = 0; i < 12; ++i)
        Bind(window, "status/iconAw" + Number(i), [this, i] {
          if (game->GetWorld()->plotgenerator.PlayerCompletedSpecialMission(i))
            popup.ShowInfo("AWARD", game->GetWorld()->plotgenerator.SpecialMissionTitle(i));
        });
      break;
    case Tour:
      Bind(window, "btnMin", [this] { tourMinimized = !tourMinimized; });
      Bind(window, "btnNext", [this] {
        if (Tutorial *tutorial = TutorialTask()) {
          if (tutorial->GetCurrentTutorial() == 0) tutorial->RunTutorial(1, 1);
          else LegacyClick(TutorialName("tutorial_next").c_str());
        }
      });
      Bind(window, "btnPrev", [this] {
        if (Tutorial *tutorial = TutorialTask()) {
          if (tutorial->GetCurrentPart() > 1)
            tutorial->RunTutorial(tutorial->GetCurrentTutorial(), tutorial->GetCurrentPart() - 1);
          else tutorial->RunMenu();
        }
      });
      break;
    case Analyser:
      for (int i = 0; i < 3; ++i)
        Bind(window, "btnSystem" + Number(i), [this, i] { TargetSecurity(i); });
      break;
    case Cheats:
#ifdef TESTGAME
      {
        const char *controls[] = {"btnLinks", "btnSoftware", "btnHardware", "btnMoney",
          "btnNRating", "btnARating", "btnTrace", "btnGame", "btnRevelation"};
        const char *legacy[] = {"cheat_alllinks", "cheat_allsoftware", "cheat_allhardware",
          "cheat_money", "cheat_nextrating", "cheat_maxratings", "cheat_canceltrace",
          "cheat_endgame", "cheat_revelation"};
        for (int i = 0; i < 9; ++i) {
          const std::string action = legacy[i];
          Bind(window, controls[i], [action] {
            WithLegacyScreen(SCREEN_CHEATS, -1, [action] { LegacyClick(action.c_str()); });
          });
        }
      }
#endif
      break;
    case GatewayView:
      Visible(root, "txtCompRef", false);
      break;
    default: break;
    }
    return true;
  }

  void SetLegacyVisuals(int screen) {
    const struct { int screen; const char *prefix; } groups[] = {
        {SCREEN_HW, "hw_"}, {SCREEN_GATEWAY, "gateway_"},
        {SCREEN_MEMORY, "memory_"}, {SCREEN_STATUS, "status_"},
        {SCREEN_FINANCE, "finance_"}, {SCREEN_EMAIL, "email_"},
        {SCREEN_MISSION, "mission_"}, {SCREEN_SENDMAIL, "sendmail_"},
        {SCREEN_ANALYSER, "analyser_"}, {SCREEN_LAN, "lan_"},
        {SCREEN_CHEATS, "cheat_"}};
    bool mirrored = false;
    for (const auto &group : groups) {
      const bool active = group.screen == screen;
      mirrored = mirrored || active;
      EclSetButtonsVisibleByPrefix(group.prefix, !active);
    }
    EclSetButtonsVisibleByPrefix("localint_background", !mirrored);
  }

  void SetLegacyScreen(int screen, int index = -1) {
    LocalInterface *localInterface =
        game->GetInterface()->GetLocalInterface();
    if (localInterface->InScreen() == screen &&
        (screen == SCREEN_NONE || localInterface->screenindex == index)) return;
    if (localInterface->InScreen() == SCREEN_SENDMAIL && mailTab == 2) StoreDraft();
    localInterface->RunScreen(screen, index);
    localInterface->Update();
    SetLegacyVisuals(screen);
  }
  bool OwnsLegacyScreen(Window window, int screen) const {
    return (window == System && (screen == SCREEN_HW || screen == SCREEN_GATEWAY)) ||
           (window == Files && screen == SCREEN_MEMORY) ||
           (window == User && (screen == SCREEN_STATUS || screen == SCREEN_FINANCE)) ||
           (window == Analyser && screen == SCREEN_ANALYSER) ||
           (window == Cheats && screen == SCREEN_CHEATS) ||
           (window == Email && (screen == SCREEN_EMAIL || screen == SCREEN_MISSION ||
                                screen == SCREEN_SENDMAIL));
  }
  void SyncLegacyWindow(Window window) {
    if (!PlayerModel()) return;
    if (window == System) SetLegacyScreen(SCREEN_HW);
    else if (window == Files && !selectingAttachment) SetLegacyScreen(SCREEN_MEMORY);
    else if (window == User) SetLegacyScreen(userTab == 0 ? SCREEN_STATUS : SCREEN_FINANCE);
    else if (window == Email) {
      if (mailTab == 2) SetLegacyScreen(SCREEN_SENDMAIL);
      else if (mailTab == 0 && MessageIndex(message) >= 0)
        SetLegacyScreen(SCREEN_EMAIL, MessageIndex(message));
      else if (mailTab == 1 && MissionIndex(mission) >= 0)
        SetLegacyScreen(SCREEN_MISSION, MissionIndex(mission));
      else if (OwnsLegacyScreen(Email, game->GetInterface()->GetLocalInterface()->InScreen()))
        SetLegacyScreen(SCREEN_NONE);
    }
  }

  bool Show(Window window, bool synchronize = true) {
#ifndef TESTGAME
    if (window == Cheats) return false;
#endif
    if (!PlayerModel() || !Prepare(window)) return false;
    if (window == OptionsWindow && !View(window).IsVisible()) SnapshotOptions();
    if (!View(window).Show()) return false;
    if (synchronize) SyncLegacyWindow(window);
    if (window == GatewayView) owner.SetFocus(Child(Root(window), "controls/btnClose"));
    return true;
  }
  bool Toggle(Window window) {
    if (View(window).IsVisible()) { Close(window); return true; }
    return Show(window);
  }
  void Close(Window window) {
    if (window == Tour) {
      if (TutorialTask()) LegacyClick(TutorialName("tutorial_close").c_str());
      tutorialPID = -1;
      hiddenTutorial.clear();
      tourSuppressed = true;
    }
    if (window == Email && mailTab == 2) StoreDraft();
    if (window == Files || window == Email) {
      fileMenu.Hide();
      contextFile = nullptr;
      if (window == Email) selectingAttachment = false;
    }
    if (PlayerModel() && OwnsLegacyScreen(
            window, game->GetInterface()->GetLocalInterface()->InScreen()))
      SetLegacyScreen(SCREEN_NONE);
    View(window).Hide();
  }
  void Hide() {
    if (View(Email).IsVisible() && mailTab == 2 && PlayerModel()) StoreDraft();
    popup.Hide();
    fileMenu.Hide();
    contextFile = nullptr;
    systemMenuOpen = false;
    menuDismissKey = -1;
    for (auto &view : views) view->Hide();
    SetLegacyVisuals(SCREEN_NONE);
    RestoreTutorial();
    tourSuppressed = true;
  }

  int MessageIndex(Message *target) const {
    Player *player = PlayerModel();
    if (player && target)
      for (int i = 0; i < player->messages.Size(); ++i)
        if (player->messages.GetData(i) == target) return i;
    return -1;
  }
  int MissionIndex(Mission *target) const {
    Player *player = PlayerModel();
    if (player && target)
      for (int i = 0; i < player->missions.Size(); ++i)
        if (player->missions.GetData(i) == target) return i;
    return -1;
  }
  int DataIndex(Data *target) const {
    Player *player = PlayerModel();
    if (player && target) {
      DataBank &db = player->gateway.databank;
      for (int i = 0; i < db.GetDataSize(); ++i)
        if (db.GetDataFile(i) == target) return i;
    }
    return -1;
  }
  const std::vector<Message *> &OrderedMessages() {
    Player *player = PlayerModel();
    const int count = player ? player->messages.Size() : 0;
    bool changed = mailboxSnapshot.size() != static_cast<std::size_t>(count);
    mailboxSnapshot.resize(count);
    for (int i = 0; i < count; ++i) {
      Message *entry = player->messages.GetData(count - i - 1);
      changed = changed || mailboxSnapshot[i] != entry;
      mailboxSnapshot[i] = entry;
    }
    if (changed) {
      orderedMessages = mailboxSnapshot;
      orderedMessages.erase(std::remove(orderedMessages.begin(), orderedMessages.end(), nullptr),
                            orderedMessages.end());
      std::stable_sort(orderedMessages.begin(), orderedMessages.end(), [](Message *a, Message *b) {
        return b->date.Before(&a->date);
      });
    }
    return orderedMessages;
  }
  void OpenMail(Message *entry) {
    if (MessageIndex(entry) < 0) return;
    if (mailTab == 2) StoreDraft();
    message = entry; mission = nullptr; mailTab = 0;
    selectingAttachment = false;
    if (!Show(Email)) return;
    entry->SetRead(true);
    if (auto *scroll = View(Email).FindAs<UIScrollableList>("viewMessage/contentsScroll"))
      scroll->SetScroll(0, 0);
  }
  void OpenMission(Mission *entry) {
    if (MissionIndex(entry) < 0) return;
    if (mailTab == 2) StoreDraft();
    message = nullptr; mission = entry; mailTab = 1;
    selectingAttachment = false;
    if (!Show(Email)) return;
    entry->SetRead(true);
  }
  void NavigateMail(int direction) {
    if (mailTab == 0) {
      const auto &ordered = OrderedMessages();
      auto found = std::find(ordered.begin(), ordered.end(), message);
      if (found == ordered.end()) {
        if (!ordered.empty()) OpenMail(ordered.front());
      } else {
        const auto next = (found - ordered.begin()) + direction;
        if (next >= 0 && next < static_cast<std::ptrdiff_t>(ordered.size()))
          OpenMail(ordered[static_cast<std::size_t>(next)]);
      }
    } else if (mailTab == 1) {
      Player *player = PlayerModel();
      const int current = MissionIndex(mission);
      const int next = current - direction;
      if (player && next >= 0 && next < player->missions.Size())
        OpenMission(player->missions.GetData(next));
    }
  }
  template <class Mail> std::string Body(Mail *mail, const char *body) {
    std::ostringstream contents;
    contents << (body ? body : "");
    if (mail->links.Size()) contents << "\n\nLinks included:\n";
    for (int i = 0; i < mail->links.Size(); ++i) {
      char *ip = mail->links.GetData(i);
      VLocation *location = game->GetWorld()->GetVLocation(ip);
      Computer *computer = location ? location->GetComputer() : nullptr;
      contents << "- " << (computer ? computer->name : ip) << '\n';
    }
    std::unique_ptr<DArray<char *>> codes(mail->codes.ConvertToDArray());
    std::unique_ptr<DArray<char *>> names(mail->codes.ConvertIndexToDArray());
    if (codes->NumUsed()) contents << "\nCodes included:\n";
    for (int i = 0; i < codes->Size(); ++i) {
      if (!codes->ValidIndex(i) || !names->ValidIndex(i)) continue;
      char *ip = names->GetData(i);
      VLocation *location = game->GetWorld()->GetVLocation(ip);
      Computer *computer = location ? location->GetComputer() : nullptr;
      contents << '[' << (computer ? computer->name : ip) << "]\n- "
               << codes->GetData(i) << '\n';
    }
    return contents.str();
  }
  void UpdateEmail() {
    if (!View(Email).IsVisible()) return;
    Player *player = PlayerModel();
    if (mailTab == 2 &&
        game->GetInterface()->GetLocalInterface()->InScreen() == SCREEN_SENDMAIL)
      StoreDraft();
    if (MessageIndex(message) < 0) message = nullptr;
    if (MissionIndex(mission) < 0) mission = nullptr;
    UIObject *root = Root(Email);
    const bool viewing = mailTab != 2 && (message || mission);
    Visible(root, "list", !viewing && mailTab != 2);
    Visible(root, "viewMessage", viewing);
    Visible(root, "composeMessage", mailTab == 2);
    Visible(root, "btnPrevMessage", mailTab != 2);
    Visible(root, "btnNextMessage", mailTab != 2);
    Tab(Email, mailTab);
    bool unread = false, unreadMission = false;
    emailRows.Begin();
    float y = 0;
    UIObject *list = Child(root, "list");
    // Explicit placement permits newest-first reconciliation without detaching
    // the focused row or rebuilding a callback that may still be on the stack.
    list->arrangeType = ArrangeType::ArrangeFree;
    const auto &ordered = OrderedMessages();
    for (Message *entry : ordered) {
      unread = unread || !entry->IsRead();
      if (mailTab != 0) continue;
      UIObject *row = emailRows.Get(list, "mail:" + Key(entry), emailFactory);
      if (!row) continue;
      row->y = y; y += row->h;
      Text(row, "txtSubject", entry->GetSubject());
      Text(row, "txtDetails", std::string(entry->date.GetShortString()) + " | " + entry->from);
      Visible(row, "unread", !entry->IsRead());
      Visible(row, "attachIcon", entry->GetData() != nullptr);
      if (!Child(row, "btnBase")->onClickCallback)
        Child(row, "btnBase")->onClickCallback = [this, entry](UIObject *) { OpenMail(entry); };
    }
    for (int i = player->missions.Size() - 1; i >= 0; --i) {
      Mission *entry = player->missions.GetData(i);
      if (!entry) continue;
      unreadMission = unreadMission || !entry->IsRead();
      if (mailTab != 1) continue;
      UIObject *row = emailRows.Get(list, "mission:" + Key(entry), emailFactory);
      if (!row) continue;
      row->y = y; y += row->h;
      Text(row, "txtSubject", entry->description);
      Text(row, "txtDetails", std::string(entry->employer) + " | " + Number(entry->payment, " credits"));
      Visible(row, "unread", !entry->IsRead());
      Visible(row, "attachIcon", false);
      if (!Child(row, "btnBase")->onClickCallback)
        Child(row, "btnBase")->onClickCallback = [this, entry](UIObject *) { OpenMission(entry); };
    }
    emailRows.End();
    Visible(root, "btnTab0/notif", unread);
    Visible(root, "btnTab1/notif", unreadMission);
    if (!viewing) return;
    Text(root, "viewMessage/txtFrom", mission ? "Employer" : "From");
    Text(root, "viewMessage/txtDate", mission ? "Payment" : "Date");
    Text(root, "viewMessage/txtFromDetails", mission ? mission->employer : message->from);
    Text(root, "viewMessage/txtDateDetails", mission ? Number(mission->payment, " credits") : message->date.GetShortString());
    Text(root, "viewMessage/txtSubjectDetails", mission ? mission->description : message->GetSubject());
    std::string body = mission ? Body(mission, mission->GetFullDetails()) : Body(message, message->GetBody());
    if (mission && mission->GetTarget() && *mission->GetTarget())
      body += "\n\nTarget: " + std::string(mission->GetTarget());
    if (message && message->GetData()) body += "\n\nData included:\n" + std::string(message->GetData()->title);
    UIObject *contents = Child(root, "viewMessage/contentsScroll/txtContents");
    if (contents && GetText(contents) != body) {
      SetText(contents, body);
      if (auto *paragraph = dynamic_cast<UITextParagraph *>(contents)) {
        paragraph->PrepareText();
        contents->h = std::max(1.0f, static_cast<float>(paragraph->lines.size())) * (paragraph->fontSize + paragraph->leading + 3);
      }
    }
    Visible(root, "viewMessage/btnAttachment", message && message->GetData());
    Text(root, "viewMessage/btnDelete", mission ? "Abandon" : "Delete");
  }
  void SaveAttachment() {
    const int index = MessageIndex(message);
    if (index >= 0 && message->GetData())
      WithLegacyScreen(SCREEN_EMAIL, index, [] { LegacyClick("email_attachment"); });
  }
  void Reply() {
    const int index = message ? MessageIndex(message) : MissionIndex(mission);
    if (index < 0) return;
    const std::string to = mission ? mission->contact : message->from;
    const std::string subject = mission ? "Mission completed" : "RE your email";
    const std::string body = mission
      ? "I have completed the following mission:\n" + std::string(mission->description) +
        "\n\nPlease credit my account with the remaining payment ASAP.\nAdditional requested data:\n"
      : "RE your email\n'" + std::string(message->GetSubject()) + "'\n\n";
    SetLegacyScreen(mission ? SCREEN_MISSION : SCREEN_EMAIL, index);
    if (!LegacyClick(mission ? "mission_reply" : "email_reply")) return;
    // The real callback owns the SCREEN_SENDMAIL transition. Populate its
    // retained draft immediately rather than waiting for caption animations.
    mailTab = 2;
    Text(Root(Email), "composeMessage/btnTo", to);
    Text(Root(Email), "composeMessage/btnSubject", subject);
    Text(Root(Email), "composeMessage/btnContents", body);
    StoreDraft();
    // Recreate the real controls from the synchronized draft to discard the
    // callback's delayed caption animation before the user can edit the reply.
    game->GetInterface()->GetLocalInterface()->RunScreen(SCREEN_SENDMAIL);
    game->GetInterface()->GetLocalInterface()->Update();
    LoadDraft();
    message = nullptr; mission = nullptr;
  }
  void DeleteMail() {
    if (message) {
      const int index = MessageIndex(message);
      if (index >= 0) WithLegacyScreen(SCREEN_EMAIL, index, [] { LegacyClick("email_delete"); });
      message = nullptr;
    } else if (mission && MissionIndex(mission) >= 0) {
      Mission *selected = mission;
      popup.ShowConfirm("ABANDON MISSION?", mission->description, [this, selected] {
        const int index = MissionIndex(selected);
        if (index >= 0) WithLegacyScreen(SCREEN_MISSION, index, [] {
          LegacyClick("mission_abandon");
          LegacyClick("mission_abandon");
        });
        mission = nullptr;
      });
    }
  }
  SendMailInterface *Composer() const {
    LocalInterface *localInterface =
        game->GetInterface()->GetLocalInterface();
    return localInterface->InScreen() == SCREEN_SENDMAIL
        ? static_cast<SendMailInterface *>(localInterface->GetInterfaceScreen())
        : nullptr;
  }
  void LoadDraft() {
    WithLegacyScreen(SCREEN_SENDMAIL, -1, [this] {
      Message *draft = Composer()->sGetMessage();
      if (!draft) return;
      Text(Root(Email), "composeMessage/btnTo", draft->to);
      Text(Root(Email), "composeMessage/btnSubject", draft->GetSubject());
      Text(Root(Email), "composeMessage/btnContents", draft->GetBody());
      attachment = draft->GetData();
      Text(Root(Email), "composeMessage/btnAttach", attachment ? attachment->title : "File Attachment");
    });
  }
  void StoreDraft() {
    if (!Root(Email)) return;
    WithLegacyScreen(SCREEN_SENDMAIL, -1, [this] {
      Message *draft = Composer()->sGetMessage();
      if (!draft) return;
      auto *to = View(Email).FindAs<UIButtonTextField>("composeMessage/btnTo");
      auto *subject = View(Email).FindAs<UIButtonTextField>("composeMessage/btnSubject");
      auto *body = View(Email).FindAs<UIButtonTextField>("composeMessage/btnContents");
      const auto sync = [](const UIButtonTextField *field, const char *name) {
        Button *button = LegacyFindButton(name);
        return field && button &&
            (field->inputText == button->caption || SyncLegacyInput(field, name));
      };
      if (!sync(to, "sendmail_to") || !sync(subject, "sendmail_subject") ||
          !sync(body, "sendmail_body box")) return;
      if (to->inputText != draft->to) draft->SetTo(const_cast<char *>(to->inputText.c_str()));
      if (subject->inputText != draft->GetSubject())
        draft->SetSubject(const_cast<char *>(subject->inputText.c_str()));
      if (body->inputText != draft->GetBody())
        draft->SetBody(const_cast<char *>(body->inputText.c_str()));
      attachment = draft->GetData();
    });
  }
  void ClearDraftFields() {
    Text(Root(Email), "composeMessage/btnTo", "");
    Text(Root(Email), "composeMessage/btnSubject", "");
    Text(Root(Email), "composeMessage/btnContents", "");
    Text(Root(Email), "composeMessage/btnAttach", "File Attachment");
    attachment = nullptr;
    selectingAttachment = false;
  }
  void CancelDraft() {
    SetLegacyScreen(SCREEN_SENDMAIL);
    LegacyClick("sendmail_clear");
    // Clear updates the static draft first. Refresh captions before Remove,
    // which otherwise copies the old editable captions back into that draft.
    game->GetInterface()->GetLocalInterface()->Update();
    ClearDraftFields();
    mailTab = 0;
    message = nullptr; mission = nullptr;
    SetLegacyScreen(SCREEN_NONE);
  }
  void Send() {
    SetLegacyScreen(SCREEN_SENDMAIL);
    StoreDraft();
    if (!LegacyClick("sendmail_send")) return;
    ClearDraftFields();
    mailTab = 0;
    Close(Email);
  }


  bool IsAudio(Data *data) {
    if (data->TYPE == DATATYPE_PROGRAM) return false;
    std::istringstream name(data->title);
    std::string ip;
    name >> ip;
    VLocation *location = game->GetWorld()->GetVLocation(const_cast<char *>(ip.c_str()));
    Computer *computer = location ? location->GetComputer() : nullptr;
    return computer && computer->TYPE == COMPUTER_TYPE_VOICEPHONESYSTEM;
  }
  float FileProgress(Data *data) {
    DataBank *db = &PlayerModel()->gateway.databank;
    for (int i = 0; i < SvbNumTasks(); ++i) {
      TaskWrapper *wrapper = SvbGetTaskWrapperAtIndex(i);
      if (!wrapper || !wrapper->task) continue;
      int index = -1, progress = 0, required = 0;
      const DataBank *source = nullptr;
      if (auto *task = dynamic_cast<FileDeleter *>(wrapper->task)) {
        source = task->GetSource(); index = task->GetSourceIndex();
        progress = task->GetProgress(); required = task->GetTicksRequired();
      } else if (auto *task = dynamic_cast<FileCopier *>(wrapper->task)) {
        source = task->GetSource(); index = task->GetSourceIndex();
        progress = task->GetProgress(); required = task->GetTicksRequired();
      } else if (auto *task = dynamic_cast<Decrypter *>(wrapper->task)) {
        source = task->GetSource(); index = task->GetSourceIndex();
        progress = task->GetProgress(); required = task->GetTicksRequired();
      }
      if (source == db && index >= 0 && db->GetData(index) == data)
        return required > 0
            ? std::min(1.0f, std::max(0.0f, static_cast<float>(progress) / required)) : 0;
    }
    return -1;
  }
  void UpdateFiles() {
    if (!View(Files).IsVisible()) return;
    UIObject *root = Root(Files);
    UIObject *list = Child(root, "list");
    DataBank &db = PlayerModel()->gateway.databank;
    Tab(Files, fileTab);
    int used = 0;
    fileRows.Begin();
    float y = 0;
    list->arrangeType = ArrangeType::ArrangeFree;
    for (int i = 0; i < db.GetDataSize(); ++i) {
      Data *data = db.GetDataFile(i);
      if (!data) continue;
      used += data->size;
      const bool app = data->TYPE == DATATYPE_PROGRAM;
      const bool audio = !app && IsAudio(data);
      if (!selectingAttachment && ((fileTab == 0 && !app) ||
          (fileTab == 1 && (app || audio)) || (fileTab == 2 && !audio))) continue;
      UIObject *row = fileRows.Get(list, "file:" + Key(data), fileFactory);
      if (!row) continue;
      row->y = y; y += row->h;
      Text(row, "txtName", DisplayName(data->title) + (app ? " " + Version(data->version) : ""));
      Text(row, "txtSize", Number(data->size, " GQs"));
      Text(row, "txtEncr", data->encrypted ? "[Level " + Number(data->encrypted) + "]" : "");
      Image(row, "icon", app ? "winIcon_app.png" : audio ? "appBtn_play_s.png" : "winIcon_file.png");
      float progress = FileProgress(data);
      if (UIObject *bar = Child(row, "progressBar")) {
        bar->visible = progress >= 0;
        bar->w = 451 * std::max(0.0f, progress);
      }
      if (!Child(row, "btnBase")->onClickCallback)
        Child(row, "btnBase")->onClickCallback = [this, data](UIObject *) { FileClick(data); };
    }
    fileRows.End();
    Text(root, "hddSpace/txtAmount", Number(std::max(0, db.GetSize() - used), " GQs"));
    if (UIObject *bar = Child(root, "hddSpace/barFill"))
      bar->scaleX = db.GetSize() ? std::min(1.0f, std::max(0.0f, static_cast<float>(used) / db.GetSize())) : 0;
  }
  void TargetMemory(int memoryIndex) {
    UplinkTask *target = game->GetInterface()->GetTaskManager()->GetTargetProgram();
    if (target && !target->IsInterfaceVisible()) target->CreateInterface();
    WithLegacyScreen(SCREEN_MEMORY, -1, [memoryIndex] {
      char name[] = "memory_block 0";
      // Existing tasks use this real Eclipse control as their animation anchor,
      // while the explicit memory index remains the model-authoritative target.
      game->GetInterface()->GetTaskManager()->SetProgramTarget(
          &game->GetWorld()->GetPlayer()->gateway.databank, name, memoryIndex);
    });
  }
  void TargetFreeSpace() {
    if (!game->GetInterface()->GetTaskManager()->GetTargetProgram()) return;
    DataBank &db = PlayerModel()->gateway.databank;
    for (int i = 0; i < db.GetSize(); ++i)
      if (!db.GetData(i)) { TargetMemory(i); return; }
    popup.ShowInfo("FILES MANAGER", "There is no free space on your Gateway.");
  }
  void FileClick(Data *data) {
    const int index = DataIndex(data);
    if (index < 0) return;
    View(Files).BringToFront();
    if (selectingAttachment) {
      SetLegacyScreen(SCREEN_SENDMAIL);
      StoreDraft();
      Message *draft = Composer()->sGetMessage();
      if (!draft) return;
      draft->AttachData(data);
      attachment = draft->GetData();
      Text(Root(Email), "composeMessage/btnAttach", attachment->title);
      selectingAttachment = false;
      View(Email).BringToFront();
      return;
    }
    TaskManager *tasks = game->GetInterface()->GetTaskManager();
    if (tasks->GetTargetProgram()) {
      TargetMemory(PlayerModel()->gateway.databank.GetMemoryIndex(index));
    } else if (data->TYPE == DATATYPE_PROGRAM) Launch(data->title, data->version);
    else if (IsAudio(data)) {
      if (!FileAction(data, PlayFile))
        popup.ShowInfo("VOICE ANALYSER", "Voice_Analyser v2.0 or later is required to load a saved voice sample.");
    } else {
      popup.ShowInfo(data->title, Number(data->size, " GQs") +
        (data->encrypted ? "\nEncryption level " + Number(data->encrypted) : "\nNot encrypted"));
    }
  }
  float OwnedVersion(const char *name) const {
    float version = 0;
    DataBank &db = PlayerModel()->gateway.databank;
    for (int i = 0; i < db.GetDataSize(); ++i) {
      Data *data = db.GetDataFile(i);
      if (data && data->TYPE == DATATYPE_PROGRAM && std::strcmp(data->title, name) == 0)
        version = std::max(version, data->version);
    }
    return version;
  }
  ComputerScreen *UploadDestination() const {
    if (!PlayerModel() || !PlayerModel()->IsConnected()) return nullptr;
    RemoteInterface *remote = game->GetInterface()->GetRemoteInterface();
    return remote->IsVisible() &&
           remote->GetInterfaceScreen()->ScreenID() == SCREEN_FILESERVERSCREEN
        ? remote->GetComputerScreen() : nullptr;
  }
  bool FileEligible(Data *data, FileCommand command) {
    if (DataIndex(data) < 0 || selectingAttachment) return false;
    switch (command) {
    case UploadFile: return UploadDestination() != nullptr;
    case DecryptFile:
      return data->encrypted > 0 && OwnedVersion("Decrypter") >= data->encrypted;
    case PlayFile: return IsAudio(data) && OwnedVersion("Voice_Analyser") >= 2;
    case RemoveFile: return true;
    case CancelFile: return FileProgress(data) >= 0;
    }
    return false;
  }
  void StopFileOperation(Data *data) {
    DataBank &db = PlayerModel()->gateway.databank;
    for (int i = SvbNumTasks() - 1; i >= 0; --i) {
      TaskWrapper *wrapper = SvbGetTaskWrapperAtIndex(i);
      if (!wrapper || !wrapper->task) continue;
      const DataBank *source = nullptr;
      int index = -1;
      if (auto *task = dynamic_cast<FileCopier *>(wrapper->task)) {
        source = task->GetSource(); index = task->GetSourceIndex();
      } else if (auto *task = dynamic_cast<FileDeleter *>(wrapper->task)) {
        source = task->GetSource(); index = task->GetSourceIndex();
      } else if (auto *task = dynamic_cast<Decrypter *>(wrapper->task)) {
        source = task->GetSource(); index = task->GetSourceIndex();
      }
      if (source == &db && index >= 0 && db.GetData(index) == data)
        SvbRemoveTask(wrapper->pid);
    }
  }
  bool FileAction(Data *data, FileCommand command) {
    if (!FileEligible(data, command)) return false;
    if (command == CancelFile) { StopFileOperation(data); return true; }
    if (command != PlayFile && FileProgress(data) >= 0) {
      popup.ShowInfo("FILES MANAGER", "An operation is already running on this file. Cancel it first.");
      return true;
    }
    ComputerScreen *destination = command == UploadFile ? UploadDestination() : nullptr;
    if (destination) {
      Computer *computer = destination->GetComputer();
      RemoteInterface *remote = game->GetInterface()->GetRemoteInterface();
      if (computer->security.IsRunning_Firewall()) {
        popup.ShowInfo("ACCESS DENIED", "Denied access by Firewall");
        return true;
      }
      if (remote->security_level > 2) {
        popup.ShowInfo("ACCESS DENIED", "Access Denied\nInsufficient Security Level");
        return true;
      }
      if (computer->databank.FindValidPlacement(data) < 0) {
        popup.ShowInfo("ERROR", "There is not enough disk space on the Server. Please remove some files and then try again.");
        return true;
      }
    }
    const char *software = command == UploadFile ? "File_Copier" :
        command == RemoveFile ? "File_Deleter" :
        command == DecryptFile ? "Decrypter" : "Voice_Analyser";
    const int pid = command == UploadFile || command == RemoveFile
        ? LaunchBuiltinApp(software) : LaunchOwnedApp(software);
    if (pid < 0) {
      popup.ShowInfo("WARNING", std::string(software) +
          " is unavailable. Check your installed software and running instances.");
      return true;
    }
    if (command == PlayFile) {
      auto *voice = dynamic_cast<VoiceAnalyser *>(SvbGetTask(pid));
      if (!voice) return true;
      voice->BeginLoad();
    }
    TaskManager *tasks = game->GetInterface()->GetTaskManager();
    tasks->SetTargetProgram(pid);
    TargetMemory(PlayerModel()->gateway.databank.GetMemoryIndex(DataIndex(data)));
    if (command != PlayFile && SvbGetTask(pid))
      fileOperations.push_back(pid);
    if (destination && SvbGetTask(pid)) uploads.push_back({pid, destination, data});
    return true;
  }
  void UpdateUploads() {
    for (auto upload = uploads.begin(); upload != uploads.end();) {
      auto *copy = dynamic_cast<FileCopier *>(SvbGetTask(upload->pid));
      if (!copy) { upload = uploads.erase(upload); continue; }
      ComputerScreen *destination = UploadDestination();
      if (destination != upload->destination) {
        SvbRemoveTask(upload->pid);
        upload = uploads.erase(upload);
        continue;
      }
      if (copy->GetState() == FILECOPIER_FINISHED) {
        upload = uploads.erase(upload);
        continue;
      }
      if (copy->GetState() != FILECOPIER_WAITINGFORTARGET) {
        ++upload;
        continue;
      }
      Computer *computer = destination->GetComputer();
      DataBank &bank = PlayerModel()->gateway.databank;
      Data *source = copy->GetSource() == &bank ? bank.GetData(copy->GetSourceIndex()) : nullptr;
      if (source != upload->file || DataIndex(upload->file) < 0) source = nullptr;
      RemoteInterface *remote = game->GetInterface()->GetRemoteInterface();
      if (computer->security.IsRunning_Firewall() || remote->security_level > 2 || !source ||
          computer->databank.FindValidPlacement(source) < 0) {
        popup.ShowInfo("UPLOAD FAILED", "The Server is no longer writable or has insufficient free disk space.");
        SvbRemoveTask(upload->pid);
      } else {
        char target[] = "fileserverscreen_upload";
        TaskManager *tasks = game->GetInterface()->GetTaskManager();
        tasks->SetTargetProgram(upload->pid);
        tasks->SetProgramTarget(&computer->databank, target, -1);
        copy = dynamic_cast<FileCopier *>(SvbGetTask(upload->pid));
        if (copy && copy->GetState() == FILECOPIER_WAITINGFORTARGET) {
          popup.ShowInfo("UPLOAD FAILED", "The upload could not be started.");
          SvbRemoveTask(upload->pid);
        }
      }
      upload = uploads.erase(upload);
    }
  }
  void CompleteFileOperations() {
    for (auto operation = fileOperations.begin(); operation != fileOperations.end();) {
      Task *task = SvbGetTask(*operation);
      bool active = false;
      if (auto *copy = dynamic_cast<FileCopier *>(task))
        active = copy->GetStatus() == FILECOPIER_INPROGRESS ||
                 copy->GetStatus() == FILECOPIER_WAITINGFORTARGET ||
                 (copy->GetStatus() == FILECOPIER_NOTDOWNLOADING && copy->GetSource());
      else if (auto *remove = dynamic_cast<FileDeleter *>(task))
        active = remove->GetStatus() == FILEDELETER_INPROGRESS ||
                 (remove->GetStatus() == FILEDELETER_OFF && remove->GetSource());
      else if (auto *decrypt = dynamic_cast<Decrypter *>(task))
        active = decrypt->GetStatus() == DECRYPTER_INPROGRESS ||
                 (decrypt->GetStatus() == DECRYPTER_OFF && decrypt->GetSource());
      if (!task || !active) {
        if (task) SvbRemoveTask(*operation);
        operation = fileOperations.erase(operation);
      } else {
        ++operation;
      }
    }
  }
  Data *FileAt(float x, float y) {
    if (!View(Files).IsVisible()) return nullptr;
    UIObject *hit = owner.HitTest(x, y);
    if (!hit) return nullptr;
    DataBank &db = PlayerModel()->gateway.databank;
    for (int i = 0; i < db.GetDataSize(); ++i) {
      Data *data = db.GetDataFile(i);
      const auto row = fileRows.objects.find("file:" + Key(data));
      if (data && row != fileRows.objects.end() && hit->IsDescendantOf(row->second.object))
        return data;
    }
    return nullptr;
  }
  bool ShowMenu(const char *const *labels, int count, float x, float y,
                const std::function<void(int)> &action) {
    if (!fileMenu.Show()) return false;
    UIObject *menu = fileMenu.Root();
    menu->interactive = true;
    const Geometry host = menu->parent->GetGeometry();
    menu->x = std::max(0.0f, std::min(1590.0f, (x - host.x) / host.sx));
    const float height = 18 + count * 43;
    menu->y = std::max(0.0f, std::min(1080.0f - height, (y - host.y) / host.sy));
    menu->w = 330; menu->h = height;
    if (UIObject *old = menu->FindChild("actions")) menu->DetachChild(old);
    if (UIObject *base = menu->FindChild("baseGFX")) { base->w = 330; base->h = height - 8; }
    if (auto *top = dynamic_cast<UILine *>(menu->FindChild("anchorTop"))) {
      top->w = 330; top->x2 = 330;
    }
    if (auto *bottom = dynamic_cast<UILine *>(menu->FindChild("anchorBottom"))) {
      bottom->y = height - 4; bottom->w = 330; bottom->x2 = 330;
    }
    for (const char *name : {"separatorRef", "btnBaseHoverRef", "btnBaseClickRef", "btnCaptionRef"})
      Visible(menu, name, false);
    std::unique_ptr<UIContainer> actions(new UIContainer);
    actions->name = "actions";
    for (int i = 0; i < count; ++i) {
      std::unique_ptr<UIObject> style = fileMenuFactory.Create();
      if (!style) continue;
      std::unique_ptr<UIButtonStandard> button(new UIButtonStandard);
      button->name = "action_" + Number(i);
      button->x = 7; button->y = 10 + i * 43; button->w = 316; button->h = 40;
      for (const char *name : {"btnBaseHoverRef", "btnBaseClickRef", "btnCaptionRef"}) {
        UIObject *object = style->FindChild(name);
        if (!object) continue;
        auto visual = style->DetachChild(object);
        visual->x = 0; visual->y = 0; visual->w = 316; visual->h = 40;
        if (!std::strcmp(name, "btnBaseHoverRef")) {
          visual->name = "gfxHover";
          button->gfxHover = visual.get();
        } else if (!std::strcmp(name, "btnBaseClickRef")) {
          visual->name = "gfxClick";
          button->gfxClick = visual.get();
        } else {
          visual->name = "txtCaption";
          visual->x = 8; visual->y = 4; SetText(visual.get(), labels[i]);
        }
        button->AddChild(std::move(visual));
      }
      button->onClickCallback = [this, action, i](UIObject *) {
        fileMenu.Hide(); contextFile = nullptr; systemMenuOpen = false;
        action(i);
      };
      actions->AddChild(std::move(button));
    }
    menu->AddChild(std::move(actions));
    owner.SetFocus(Child(menu, "action_0"));
    return true;
  }
  bool OpenFileMenu(float x, float y) {
    Data *data = FileAt(x, y);
    if (!data || selectingAttachment) return false;
    View(Files).BringToFront();
    SyncLegacyWindow(Files);
    const char *labels[] = {"Upload", "Decrypt", "Play", "Remove", "Cancel (Esc)"};
    if (!ShowMenu(labels, 5, x, y, [this, data](int action) {
          FileAction(data, static_cast<FileCommand>(action));
        })) return true;
    systemMenuOpen = false;
    contextFile = data;
    for (int i = UploadFile; i <= CancelFile; ++i)
      SetEnabled(Child(fileMenu.Root(), "action_" + Number(i)),
                 FileEligible(data, static_cast<FileCommand>(i)));
    return true;
  }
  bool OpenSystemMenu() {
    if (fileMenu.IsVisible() && systemMenuOpen) {
      fileMenu.Hide(); systemMenuOpen = false; return true;
    }
    const char *labels[] = {"Gateway Info", "About Uplink OS", "Help", "Tour Guide",
                            "Options", "Save Game", "Sign Out", "Shut Down"};
    UIObject *button = owner.FindChildRecursive("btnSystem");
    const float x = button ? button->GetAbsoluteX() : 0;
    const float y = button ? button->GetAbsoluteY() + button->GetAbsoluteHeight() : 60;
    if (!ShowMenu(labels, 8, x, y, [this](int action) {
          switch (action) {
          case 0: systemTab = 0; Show(System); break;
          case 1: systemTab = 1; Show(System); break;
          case 2: HDUIManager::GetInstance().Invoke("Open Internal Help"); break;
          case 3: Invoke("Open Tour Guide"); break;
          case 4: Show(OptionsWindow); break;
          case 5: Invoke("Save Game"); break;
          case 6: HDUIManager::GetInstance().Invoke("Log Out"); break;
          case 7:
            popup.ShowConfirm("Shut down",
                "Are you sure you want to shut down Uplink?\n\nProgress will be saved.",
                [] { HDUIManager::GetInstance().Invoke("Quit"); });
            break;
          }
        })) return false;
    contextFile = nullptr; systemMenuOpen = true;
    return true;
  }
  bool EmailContext() {
    if (!View(Email).IsVisible() || mailTab == 2 ||
        (MessageIndex(message) < 0 && MissionIndex(mission) < 0)) return false;
    UIObject *hit = owner.HitTest(pointerX, pointerY);
    UIObject *focus = owner.GetFocus();
    if (hit) return hit->IsDescendantOf(Root(Email));
    const int screen = game->GetInterface()->GetLocalInterface()->InScreen();
    return (focus && focus->IsDescendantOf(Root(Email))) ||
           screen == SCREEN_EMAIL || screen == SCREEN_MISSION;
  }

  bool Owned(const std::string &name, float version) {
    Gateway &gateway = PlayerModel()->gateway;
    const int upgrade = HUDUpgrade(name);
    if (upgrade) return gateway.HasHUDUpgrade(upgrade);
    DataBank &db = gateway.databank;
    for (int i = 0; i < db.GetDataSize(); ++i) {
      Data *data = db.GetDataFile(i);
      if (data && data->TYPE == DATATYPE_PROGRAM && name == data->title && data->version == version) return true;
    }
    return false;
  }
  bool Launch(const std::string &name, float version) {
    if (!PlayerModel() || !Owned(name, version)) return false;
    if (name == "HUD_ConnectionAnalysis") { Toggle(Analyser); return true; }
    if (name == "HUD_LANView") {
      game->GetInterface()->GetLocalInterface()->RunScreen(SCREEN_LAN);
      View(Apps).Hide(); return true;
    }
    if (name == "HUD_IRC-Client") {
      game->GetInterface()->GetLocalInterface()->RunScreen(SCREEN_IRC);
      View(Apps).Hide(); return true;
    }
    if (name == "HUD_MapShowTrace") {
      popup.ShowInfo("HUD MapShowTrace", "Show trace on map is installed and active.");
      return true;
    }
    game->GetInterface()->GetTaskManager()->RunSoftware(const_cast<char *>(name.c_str()), version);
    View(Apps).Hide();
    if (name == "Tutorial") { tourSuppressed = false; Show(Tour); }
    return true;
  }
  Sale *FindSale(const std::string &name) {
    auto *company = dynamic_cast<CompanyUplink *>(game->GetWorld()->GetCompany(const_cast<char *>("Uplink")));
    if (company)
      for (int i = 0; i < company->sw_sales.Size(); ++i) {
        Sale *sale = company->GetSWSale(i);
        if (sale && name == sale->title) return sale;
      }
    return nullptr;
  }
  SaleVersion *FindVersion(Sale *sale, float version) {
    if (sale)
      for (int i = 0; i < sale->versions.Size(); ++i)
        if (sale->versions.ValidIndex(i)) {
          SaleVersion *entry = sale->GetVersion(i);
          if (entry && static_cast<float>(entry->data) == version) return entry;
        }
    return nullptr;
  }
  bool Buy(const std::string &name, float version) {
    Sale *sale = FindSale(name);
    SaleVersion *entry = FindVersion(sale, version);
    if (!sale || !entry) return false;
    if (Owned(name, version)) {
      popup.ShowInfo("SOFTWARE", "This version is already installed."); return true;
    }
    Gateway &gateway = PlayerModel()->gateway;
    if (sale->swhwTYPE == SOFTWARETYPE_BYPASSER && !gateway.HasHUDUpgrade(HUDUPGRADE_CONNECTIONANALYSIS)) {
      popup.ShowInfo("SOFTWARE", "You require a HUD_ConnectionAnalysis upgrade before purchasing any bypasser."); return true;
    }
    if (sale->swhwTYPE == SOFTWARETYPE_LANTOOL && !gateway.HasHUDUpgrade(HUDUPGRADE_LANVIEW)) {
      popup.ShowInfo("SOFTWARE", "You require a HUD_LanView upgrade before purchasing any LAN tools."); return true;
    }
    if (PlayerModel()->GetBalance() < entry->cost) {
      popup.ShowInfo("SOFTWARE", "You do not have enough credits to purchase this program."); return true;
    }
    const int upgrade = HUDUpgrade(name);
    if (sale->swhwTYPE == SOFTWARETYPE_HUDUPGRADE && !upgrade) return false;
    std::unique_ptr<Data> data(new Data);
    data->SetTitle(sale->title);
    data->SetDetails(DATATYPE_PROGRAM, entry->size, 0, 0, static_cast<float>(entry->data), sale->swhwTYPE);
    const int placement = gateway.databank.FindValidPlacement(data.get());
    if (!upgrade && placement < 0) {
      popup.ShowInfo("SOFTWARE", "You do not have enough memory to store this program."); return true;
    }
    if (upgrade) gateway.GiveHUDUpgrade(upgrade);
    else gateway.databank.PutData(data.release(), placement);
    PlayerModel()->ChangeBalance(-entry->cost, const_cast<char *>("Uplink Corporation Software sales"));
    return true;
  }
  void AppClick(const std::string &name) {
    const auto selected = appVersions.find(name);
    if (selected == appVersions.end()) return;
    const float version = selected->second;
    if (Owned(name, version)) Launch(name, version);
    else {
      SaleVersion *entry = FindVersion(FindSale(name), version);
      if (entry) popup.ShowConfirm("PURCHASE SOFTWARE?", DisplayName(name) + " " + Version(version) +
          "\n" + Number(entry->cost, " credits") + "\n" + entry->GetDetails(),
          [this, name, version] { Buy(name, version); });
    }
  }
  void CycleAppVersion(const std::string &name) {
    std::set<float> versions;
    DataBank &db = PlayerModel()->gateway.databank;
    for (int i = 0; i < db.GetDataSize(); ++i) {
      Data *data = db.GetDataFile(i);
      if (data && name == data->title) versions.insert(data->version);
    }
    if (Sale *sale = FindSale(name))
      for (int i = 0; i < sale->versions.Size(); ++i)
        if (sale->versions.ValidIndex(i))
          if (SaleVersion *entry = sale->GetVersion(i)) versions.insert(static_cast<float>(entry->data));
    if (versions.empty()) return;
    auto selected = versions.upper_bound(appVersions[name]);
    appVersions[name] = selected == versions.end() ? *versions.begin() : *selected;
  }
  UIObject *Category(int type, const std::string &title) {
    auto found = categoryRoots.find(type);
    if (found != categoryRoots.end()) return found->second;
    UIObject *list = Child(Root(Apps), "appList");
    if (!list) return nullptr;
    std::unique_ptr<UIObject> category(new UIContainer);
    category->name = "category:" + Number(type);
    category->w = 480;
    std::unique_ptr<UIObject> label = categoryFactory.Create();
    std::unique_ptr<UIObject> separator = separatorFactory.Create();
    if (!label || !separator) return nullptr;
    label->x = 20; label->y = 0; label->visible = true;
    SetText(label.get(), title);
    separator->x = 20; separator->y = 32; separator->visible = true;
    category->AddChild(std::move(label));
    category->AddChild(std::move(separator));
    UIObject *result = category.get();
    list->AddChild(std::move(category));
    categoryRoots.emplace(type, result);
    return result;
  }
  void UpdateApps() {
    if (!View(Apps).IsVisible()) return;
    struct App { std::string name; float version; int type; };
    std::vector<App> apps;
    DataBank &db = PlayerModel()->gateway.databank;
    for (int i = 0; i < db.GetDataSize(); ++i) {
      Data *data = db.GetDataFile(i);
      if (!data || data->TYPE != DATATYPE_PROGRAM) continue;
      auto found = std::find_if(apps.begin(), apps.end(), [data](const App &app) { return app.name == data->title; });
      if (found == apps.end()) apps.push_back({data->title, data->version, data->softwareTYPE});
      else found->version = std::max(found->version, data->version);
    }
    const char *upgrades[] = {"HUD_MapShowTrace", "HUD_ConnectionAnalysis", "HUD_IRC-Client", "HUD_LANView"};
    for (const char *name : upgrades)
      if (PlayerModel()->gateway.HasHUDUpgrade(HUDUpgrade(name))) apps.push_back({name, 1, SOFTWARETYPE_HUDUPGRADE});
    static const int categories[] = {SOFTWARETYPE_FILEUTIL, SOFTWARETYPE_HWDRIVER,
      SOFTWARETYPE_SECURITY, SOFTWARETYPE_CRACKERS, SOFTWARETYPE_BYPASSER,
      SOFTWARETYPE_LANTOOL, SOFTWARETYPE_HUDUPGRADE, SOFTWARETYPE_OTHER};
    static const char *titles[] = {"File Utilities", "Hardware Drivers", "Security", "Crackers",
      "Bypassers", "LAN tools", "HUD Upgrades", "Other"};
    appRows.Begin();
    UIObject *list = Child(Root(Apps), "appList");
    list->arrangeType = ArrangeType::ArrangeFree;
    float y = 10;
    for (int category = 0; category < 8; ++category) {
      UIObject *container = Category(categories[category], titles[category]);
      if (!container) continue;
      int count = 0;
      for (const App &app : apps) {
        int type = app.type;
        if (std::find(std::begin(categories), std::end(categories), type) == std::end(categories)) type = SOFTWARETYPE_OTHER;
        if (type != categories[category]) continue;
        auto version = appVersions.emplace(app.name, app.version);
        UIObject *row = appRows.Get(container, "app:" + app.name, appFactory);
        if (!row) continue;
        row->x = 4 + (count % 2) * 240;
        row->y = 42 + (count / 2) * 184;
        ++count;
        Text(row, "txtName", DisplayName(app.name) + " " + Version(version.first->second) +
             (Owned(app.name, version.first->second) ? "" : " (Buy)"));
        Image(row, "appIcon", "icon_app_" + std::string(AppIcon(app.name)) + ".png");
        Visible(row, "iconHotkey", false); Visible(row, "txtHotkey", false);
        UIObject *context = Child(row, "iconContext");
        context->interactive = true;
        if (!context->onClickCallback)
          context->onClickCallback = [this, name = app.name](UIObject *) { CycleAppVersion(name); };
        if (!Child(row, "btnBase")->onClickCallback)
          Child(row, "btnBase")->onClickCallback = [this, name = app.name](UIObject *) { AppClick(name); };
      }
      container->visible = count > 0;
      container->y = y;
      container->h = 42 + ((count + 1) / 2) * 184;
      if (count) y += container->h + 10;
    }
    appRows.End();
    Visible(Root(Apps), "noApps", apps.empty());
  }

  void SnapshotOptions() {
    ::Options *options = app->GetOptions();
    int width = options->GetEffectiveOptionValue("graphics_screenwidth");
    int height = options->GetEffectiveOptionValue("graphics_screenheight");
    auto *fullscreen = View(OptionsWindow).FindAs<UIButtonSelect>("btnFrameless");
    fullscreen->selected =
        options->GetEffectiveOptionValue("graphics_fullscreen") != 0;
    View(OptionsWindow).FindAs<UIButtonSelect>("btnMusic")->selected = options->GetOptionValue("sound_musicenabled") != 0;
    View(OptionsWindow).FindAs<UIButtonSelect>("btnSWMouse")->selected = options->GetOptionValue("graphics_softwaremouse") != 0;
    resolutions.clear();
    GciScreenModeList *modes = GciListScreenModes();
    if (modes) {
      for (int i = 0; i < modes->Size(); ++i) {
        if (!modes->ValidIndex(i)) continue;
        const GciScreenMode *mode = modes->GetData(i);
        if (!mode || !mode->w || !mode->h) continue;
        std::pair<int, int> dimensions(mode->w, mode->h);
        if (std::find(resolutions.begin(), resolutions.end(), dimensions) == resolutions.end()) resolutions.push_back(dimensions);
      }
      GciDeleteScreenModeArrayData(modes);
      delete modes;
    }
    const std::pair<int, int> current(width, height);
    auto found = std::find(resolutions.begin(), resolutions.end(), current);
    if (found == resolutions.end()) {
      resolution = resolutions.size(); resolutions.push_back(current);
    } else resolution = static_cast<std::size_t>(found - resolutions.begin());
    ResolutionCaption();
  }
  void ResolutionCaption() {
    if (!resolutions.empty()) Text(Root(OptionsWindow), "btnResolution",
      Number(resolutions[resolution].first) + "x" + Number(resolutions[resolution].second));
  }
  void ApplyOptions() {
    if (resolutions.empty()) return;
    ::Options *options = app->GetOptions();
    const int previousWidth =
        options->GetEffectiveOptionValue("graphics_screenwidth");
    const int previousHeight =
        options->GetEffectiveOptionValue("graphics_screenheight");
    const bool previousFullscreen =
        options->GetEffectiveOptionValue("graphics_fullscreen") != 0;
    const int requestedWidth = resolutions[resolution].first;
    const int requestedHeight = resolutions[resolution].second;
    const bool requestedFullscreen =
        View(OptionsWindow).FindAs<UIButtonSelect>("btnFrameless")->selected;
    // The live engine applies these at shutdown, exactly as its graphics options
    // screen does; changing their live values would corrupt viewport geometry.
    const bool displayChanged = requestedWidth != options->GetOptionValue("graphics_screenwidth") ||
      requestedHeight != options->GetOptionValue("graphics_screenheight") ||
      requestedFullscreen != (options->GetOptionValue("graphics_fullscreen") != 0);
    if (requestedWidth != previousWidth)
      options->RequestShutdownChange("graphics_screenwidth", requestedWidth);
    if (requestedHeight != previousHeight)
      options->RequestShutdownChange("graphics_screenheight", requestedHeight);
    if (requestedFullscreen != previousFullscreen)
      options->RequestShutdownChange("graphics_fullscreen", requestedFullscreen);
    const bool music = View(OptionsWindow).FindAs<UIButtonSelect>("btnMusic")->selected;
    if (music != (options->GetOptionValue("sound_musicenabled") != 0)) {
      options->SetOptionValue("sound_musicenabled", music);
      if (music) SgPlaylist_Play("main"); else SgPlaylist_Stop();
    }
    options->SetOptionValue("graphics_softwaremouse", View(OptionsWindow).FindAs<UIButtonSelect>("btnSWMouse")->selected);
    options->Save(nullptr);
    View(OptionsWindow).Hide();
    if (displayChanged) popup.ShowInfo("OPTIONS", "Screen setting changes will take effect when Uplink is restarted.");
  }

  void UpdateSystem() {
    if (!View(System).IsVisible()) return;
    UIObject *root = Root(System);
    Tab(System, systemTab);
    Visible(root, "gateway", systemTab == 0); Visible(root, "about", systemTab == 1);
    Gateway &gateway = PlayerModel()->gateway;
    GatewayDef *definition = gateway.curgatewaydef;
    Text(root, "gateway/txtCPUAmount", Number(gateway.GetCPUSpeed(), " GHz"));
    Text(root, "gateway/txtBandwidthAmount", Number(gateway.GetBandwidth(), " GQ/s"));
    Text(root, "gateway/txtMemoryAmount", Number(gateway.memorysize, " GQ"));
    Text(root, "gateway/txtCPUMax", Number(gateway.GetNumCPUs()) + "/" + Number(definition ? definition->maxcpus : 0, " Slots used"));
    Text(root, "gateway/txtBandwidthMax", Number(definition ? definition->bandwidth : 0, " GQ/s supported"));
    Text(root, "gateway/txtMemoryMax", Number(definition ? definition->maxmemory * 8 : 0, " GQ supported"));
    std::unique_ptr<LList<char *>> devices(gateway.GetSecurity());
    Text(root, "gateway/txtDevice01", devices->Size() ? devices->GetData(0) : "None");
    std::string other;
    for (int i = 1; i < devices->Size(); ++i) {
      if (!other.empty()) other += "\n";
      other += devices->GetData(i);
    }
    Text(root, "gateway/txtDevice02", other);
    Text(root, "gateway/txtDeviceMax", Number(gateway.GetNumSecurity()) + "/" + Number(definition ? definition->maxsecurity : 0, " Slots used"));
    std::string upgrades;
    const int bits[] = {HUDUPGRADE_MAPSHOWSTRACE, HUDUPGRADE_CONNECTIONANALYSIS, HUDUPGRADE_IRCCLIENT, HUDUPGRADE_LANVIEW};
    const char *names[] = {"Show trace on map", "Connection analysis", "IRC Client", "LAN View"};
    for (int i = 0; i < 4; ++i) if (gateway.HasHUDUpgrade(bits[i])) {
      if (!upgrades.empty()) upgrades += ", ";
      upgrades += names[i];
    }
    // GOLD has two siblings both named txtHUD: keep its heading untouched.
    if (UIObject *panel = Child(root, "gateway"))
      for (const auto &child : panel->children)
        if (child->name == "txtHUD" && dynamic_cast<UITextParagraph *>(child.get()))
          SetText(child.get(), upgrades.empty() ? "None" : upgrades);
  }
  void SelectAccount(const std::string &identity) {
    Player *player = PlayerModel();
    for (int i = 0; i < player->accounts.Size(); ++i)
      if (identity == player->accounts.GetData(i)) {
        WithLegacyScreen(SCREEN_FINANCE, -1, [i] {
          LegacyClick(("finance_account " + Number(i)).c_str());
        });
        return;
      }
  }
  void UpdateUser() {
    if (!View(User).IsVisible()) return;
    Player *player = PlayerModel();
    UIObject *root = Root(User);
    Text(root, "txtTitle", player->handle);
    Tab(User, userTab);
    Visible(root, "status", userTab == 0); Visible(root, "finance", userTab == 1);
    Text(root, "status/txtGrade", "GRADE " + Number(NUM_UPLINKRATINGS - player->rating.uplinkrating, " UPLINK AGENT"));
    Text(root, "status/txtRating", player->rating.GetUplinkRating());
    Text(root, "status/txtNeuromancer", "NEUROMANCER: " + std::string(player->rating.GetNeuromancerRating()));
    Record *record = RecordGenerator::GetCriminal(const_cast<char *>("PLAYER"));
    char *criminal = record ? record->GetField(const_cast<char *>("Convictions")) : nullptr;
    Text(root, "status/txtCriminalRec", criminal ? criminal : "None");
    for (int i = 0; i < 12; ++i) {
      const bool earned = game->GetWorld()->plotgenerator.PlayerCompletedSpecialMission(i);
      char file[48]; std::snprintf(file, sizeof(file), "iconAw%02d_24px.png", i);
      const std::string path = "status/iconAw" + Number(i) + "/iconAw" + Number(i);
      Image(root, path, earned ? file : "iconAwNone_24px.png");
    }
    UIObject *list = Child(root, "finance/list");
    list->arrangeType = ArrangeType::ArrangeFree;
    int total = 0;
    accountRows.Begin();
    for (int i = 0; i < player->accounts.Size(); ++i) {
      const std::string identity = player->accounts.GetData(i);
      std::istringstream details(identity);
      std::string ip, number; details >> ip >> number;
      BankAccount *account = BankAccount::GetAccount(const_cast<char *>(ip.c_str()), const_cast<char *>(number.c_str()));
      UIObject *row = accountRows.Get(list, "account:" + identity, accountFactory);
      if (!row) continue;
      row->y = i * row->h;
      Text(row, "txtAcc", number); Text(row, "txtBank", ip);
      Text(row, "txtBalance", account ? Number(account->balance, "c") : "Failed to log on");
      Visible(row, "gfxActive", player->currentaccount == i);
      if (account) total += account->balance;
      if (!Child(row, "btnBase")->onClickCallback)
        Child(row, "btnBase")->onClickCallback = [this, identity](UIObject *) {
          View(User).BringToFront(); SelectAccount(identity);
        };
    }
    accountRows.End();
    Text(root, "finance/txtTotalAmount", Number(total, " Credits"));
  }

  void TargetSecurity(int index) {
    Player *player = PlayerModel();
    VLocation *location = player ? player->GetRemoteHost() : nullptr;
    Computer *computer = location ? location->GetComputer() : nullptr;
    if (!computer || !computer->security.GetSystem(index)) return;
    UplinkTask *target = game->GetInterface()->GetTaskManager()->GetTargetProgram();
    if (target && !target->IsInterfaceVisible()) target->CreateInterface();
    WithLegacyScreen(SCREEN_ANALYSER, -1, [index] {
      LegacyClick(("analyser_system " + Number(index)).c_str());
    });
  }
  void UpdateAnalyser() {
    if (!View(Analyser).IsVisible()) return;
    Player *player = PlayerModel();
    VLocation *location = player->GetRemoteHost();
    Computer *computer = location ? location->GetComputer() : nullptr;
    UIObject *root = Root(Analyser);
    Text(root, "txtTitle", computer ? computer->name : "Disconnected");
    Text(root, "txtLocal", "Local\nHost");
    Text(root, "txtRemote", std::string("Remote\n") + player->remotehost);
    Visible(root, "connectionLine", player->IsConnected());
    Visible(root, "iconMonitored", false);
    for (int i = 0; i < 3; ++i) {
      SecuritySystem *system = computer ? computer->security.GetSystem(i) : nullptr;
      const std::string prefix = "system" + Number(i);
      const bool present = system != nullptr;
      Visible(root, "btnSystem" + Number(i), present);
      Visible(root, prefix + "icon", present);
      Visible(root, prefix + "status", present);
      Visible(root, prefix + "bypassed", present && system->bypassed);
      Visible(root, "txtSystem" + Number(i) + "name", present);
      if (!system) continue;
      Text(root, "txtSystem" + Number(i) + "name", system->GetName());
      const char *icon = system->TYPE == SECURITY_TYPE_FIREWALL ? "firewall" :
        system->TYPE == SECURITY_TYPE_PROXY ? "proxy" : system->TYPE == SECURITY_TYPE_MONITOR ? "monitor" : "unknown";
      if (system->TYPE != SECURITY_TYPE_ENCRYPTION)
        Image(root, prefix + "icon", "winIcon_analyser_" + std::string(icon) + ".png");
      else Image(root, prefix + "icon", "icon_cypher_01.png");
      Image(root, prefix + "status", system->bypassed ? "winIcon_analyser_statusBypassed.png" :
            system->enabled ? "winIcon_analyser_statusOn.png" : "winIcon_analyser_statusOff.png");
      if (SecurityMonitor::IsMonitored(i)) {
        UIObject *monitored = Child(root, "iconMonitored");
        UIObject *button = Child(root, "btnSystem" + Number(i));
        monitored->visible = true; monitored->x = button->x + 4;
      }
    }
  }

  Tutorial *TutorialTask() {
    return tutorialPID >= 0 ? dynamic_cast<Tutorial *>(SvbGetTask(tutorialPID)) : nullptr;
  }
  std::string TutorialName(const std::string &prefix) const { return prefix + " " + Number(tutorialPID); }
  void RestoreTutorial() {
    for (const auto &entry : hiddenTutorial)
      if (Button *button = LegacyFindButton(entry.first.c_str())) {
        button->x = entry.second.x; button->y = entry.second.y; button->Dirty();
      }
    hiddenTutorial.clear();
  }
  void HideTutorialButton(const std::string &name) {
    if (Button *button = LegacyFindButton(name.c_str())) {
      hiddenTutorial.emplace(name, HiddenButton{button->x, button->y});
      button->x = -20000; button->y = -20000;
    }
  }
  void UpdateTour() {
    Tutorial *tutorial = TutorialTask();
    if (!tutorial) {
      RestoreTutorial(); tutorialPID = -1;
      for (int i = 0; i < SvbNumTasks(); ++i) {
        TaskWrapper *wrapper = SvbGetTaskWrapperAtIndex(i);
        if (wrapper && dynamic_cast<Tutorial *>(wrapper->task)) {
          tutorialPID = wrapper->pid; tutorial = static_cast<Tutorial *>(wrapper->task); break;
        }
      }
      if (tutorial) { tourSuppressed = false; tourMinimized = false; Show(Tour); }
    }
    if (!tutorial) { View(Tour).Hide(); return; }
    if (tourSuppressed || !View(Tour).IsVisible()) return;
    if (!tutorial->IsInterfaceVisible()) tutorial->CreateInterface();
    const std::string text = LegacyCaption(TutorialName("tutorial_text").c_str());
    Text(Root(Tour), "txtContents", text);
    Text(Root(Tour), "txtTitle", "TOUR GUIDE " + Number(tutorial->GetCurrentTutorial()) +
         "/" + Number(tutorial->GetCurrentPart()));
    const char *prefixes[] = {"tutorial_title", "tutorial_skip", "tutorial_close", "tutorial_text", "tutorial_next"};
    for (const char *prefix : prefixes) HideTutorialButton(TutorialName(prefix));
    for (int i = 1; i <= 3; ++i) {
      HideTutorialButton("tutorial_part" + Number(i) + " " + Number(i) + " " + Number(tutorialPID));
      HideTutorialButton("tutorial_part" + Number(i) + "button " + Number(i) + " " + Number(tutorialPID));
    }
    UIObject *root = Root(Tour);
    const bool menu = tutorial->GetCurrentTutorial() == 0;
    Visible(root, "txtContents", !tourMinimized && !menu);
    Visible(root, "btnPrev", !tourMinimized);
    Visible(root, "btnNext", !tourMinimized && (menu || tutorial->IsNextVisible()));
    Visible(root, "base", !tourMinimized);
    Visible(root, "bottomLine", !tourMinimized);
    root->h = tourMinimized ? 30 : 382;
    Text(root, "btnNext", menu ? "Start" : "Next");
    // Menu choices reuse the final tutorial button rather than leaving the
    // existing three real tutorial selections trapped behind the HD surface.
    UIObject *choices = Child(root, "tutorialMenu");
    if (!choices) {
      std::unique_ptr<UIObject> container(new UIContainer);
      container->name = "tutorialMenu"; container->x = 12; container->y = 60;
      container->w = 450; container->h = 180;
      HDRepeatLayoutFactory buttonFactory("HUD/windows/WinTourGuide", "btnNext");
      const char *names[] = {"The basics of your screen", "Completing the first mission", "Working as an Agent"};
      for (int i = 1; i <= 3; ++i) {
        std::unique_ptr<UIObject> button = buttonFactory.Create();
        if (!button) continue;
        button->x = 0; button->y = (i - 1) * 55; button->w = 450;
        SetText(button.get(), names[i - 1]);
        if (UIObject *caption = Child(button.get(), "txtCaption")) caption->x = 225;
        button->onClickCallback = [this, i](UIObject *) {
          LegacyClick(("tutorial_part" + Number(i) + "button " + Number(i) + " " + Number(tutorialPID)).c_str());
        };
        container->AddChild(std::move(button));
      }
      choices = container.get(); root->AddChild(std::move(container));
    }
    SetVisible(choices, menu && !tourMinimized);
  }

  void UpdateGateway() {
    if (!View(GatewayView).IsVisible()) return;
    Gateway &gateway = PlayerModel()->gateway;
    GatewayDef *definition = gateway.curgatewaydef;
    if (!definition) { View(GatewayView).Hide(); return; }
    UIObject *root = Root(GatewayView);
    Text(root, "controls/txtTitle", definition->name);
    Text(root, "controls/txtDesc", definition->description);
    std::ostringstream signature;
    signature << definition << ':' << gateway.memorysize << ':' << gateway.GetBandwidth();
    for (int i = 0; i < gateway.hardware.Size(); ++i) signature << ':' << gateway.hardware.GetData(i);
    if (gatewaySignature == signature.str()) return;
    gatewaySignature = signature.str();
    UIObject *container = Child(root, "gateway");
    container->x = 610; container->y = 242;
    container->w = 700; container->h = 600;
    std::unique_ptr<LList<char *>> cpus(gateway.GetCPUs());
    std::unique_ptr<LList<char *>> security(gateway.GetSecurity());
    gatewayRows.Begin();
    // The final ViewGateway leaves its interior to the controller. Draw every
    // real slot, including empty ones, with the GOLD component atlas artwork.
    float y = 15;
    auto component = [this, &container, &y](const std::string &key, const std::string &caption,
                                          const std::string &icon) {
      UIObject *label = gatewayRows.Get(container, key, componentFactory);
      if (!label) return;
      label->visible = true; label->x = 120; label->y = y + 18;
      label->w = 560; label->h = 64;
      SetText(label, caption);
      UIObject *image = Child(label, "componentIcon");
      if (!image) {
        std::unique_ptr<UIImage> picture(new UIImage);
        picture->name = "componentIcon"; picture->x = -100; picture->y = -14;
        picture->w = 74; picture->h = 64;
        image = picture.get(); label->AddChild(std::move(picture));
      }
      Image(label, "componentIcon", icon);
      y += 76;
    };
    // A scrollable interior accommodates larger gateways instead of clipping
    // hardware that exists in the model but not in the XML's example gateway.
    UIObject *scroll = Child(container, "hardwareSlots");
    if (!scroll) {
      std::unique_ptr<UIScrollableList> slots(new UIScrollableList);
      slots->name = "hardwareSlots"; slots->w = 700; slots->h = 600;
      slots->arrangeType = ArrangeType::ArrangeFree;
      scroll = slots.get(); container->AddChild(std::move(slots));
    }
    // Rows are children of the scroller; no borrowed row is moved during a click.
    container = scroll;
    for (int i = 0; i < definition->maxcpus; ++i)
      component("cpu:" + Number(i), i < cpus->Size() ? cpus->GetData(i) : "Empty CPU slot",
                i < cpus->Size() ? "icon_gateway_cpu_01.png" : "icon_gateway_cpu_02.png");
    for (int i = 0; i < definition->maxmemory; ++i)
      component("memory:" + Number(i), i < gateway.memorysize / 8 ? "Memory (8 GQs)" : "Empty memory slot",
                i < gateway.memorysize / 8 ? "icon_gateway_memory_01.png" : "icon_gateway_memory_02.png");
    for (int i = 0; i < definition->maxsecurity; ++i)
      component("security:" + Number(i), i < security->Size() ? security->GetData(i) : "Empty security slot",
                i < security->Size() ? "icon_gateway_security_01.png" : "icon_gateway_security_03.png");
    component("modem", "Modem (" + Number(gateway.GetBandwidth(), " GQ/s)"), "icon_gateway_modem_01.png");
    component("power", "Power Supply Unit", "icon_gateway_psu_01.png");
    gatewayRows.End();
  }

  void MirrorLegacyScreen() {
    LocalInterface *localInterface =
        game->GetInterface()->GetLocalInterface();
    const int screen = localInterface->InScreen();
    const int index = localInterface->screenindex;
    switch (screen) {
    case SCREEN_STATUS:
    case SCREEN_FINANCE: {
      const int tab = screen == SCREEN_STATUS ? 0 : 1;
      if (!View(User).IsVisible() || userTab != tab) {
        userTab = tab; Show(User, false);
      }
      break;
    }
    case SCREEN_MEMORY:
      if (!View(Files).IsVisible()) Show(Files, false);
      break;
    case SCREEN_EMAIL:
      if (PlayerModel()->messages.ValidIndex(index)) {
        Message *entry = PlayerModel()->messages.GetData(index);
        if (!View(Email).IsVisible() || mailTab != 0 || message != entry) OpenMail(entry);
      }
      break;
    case SCREEN_MISSION:
      if (PlayerModel()->missions.ValidIndex(index)) {
        Mission *entry = PlayerModel()->missions.GetData(index);
        if (!View(Email).IsVisible() || mailTab != 1 || mission != entry) OpenMission(entry);
      }
      break;
    case SCREEN_SENDMAIL:
      if (!View(Email).IsVisible() || mailTab != 2) {
        if (Show(Email, false)) {
          mailTab = 2; message = nullptr; mission = nullptr;
          LoadDraft();
        }
      }
      break;
    case SCREEN_HW:
    case SCREEN_GATEWAY:
      if (!View(System).IsVisible()) { systemTab = 0; Show(System, false); }
      break;
    case SCREEN_ANALYSER:
      if (!View(Analyser).IsVisible() &&
          PlayerModel()->gateway.HasHUDUpgrade(HUDUPGRADE_CONNECTIONANALYSIS))
        Show(Analyser, false);
      break;
#ifdef TESTGAME
    case SCREEN_CHEATS:
      if (!View(Cheats).IsVisible()) Show(Cheats, false);
      break;
#endif
    default: break;
    }
    // The real screen remains authoritative for Tutorial and legacy callbacks.
    // HD views mirror it without replacing its identity with SCREEN_NONE.
  }

  void Update() {
    Player *player = PlayerModel();
    if (!player) { Hide(); playerIdentity = nullptr; return; }
    if (playerIdentity != player) {
      if (playerIdentity) Hide();
      playerIdentity = player;
      message = nullptr; mission = nullptr; attachment = nullptr;
      selectingAttachment = false; contextFile = nullptr; uploads.clear();
      tutorialPID = -1; tourSuppressed = false;
      appVersions.clear(); gatewaySignature.clear();
      mailboxSnapshot.clear(); orderedMessages.clear();
    }
    UpdateUploads();
    CompleteFileOperations();
    MirrorLegacyScreen();
    SetLegacyVisuals(game->GetInterface()->GetLocalInterface()->InScreen());
    UpdateApps(); UpdateEmail(); UpdateFiles(); UpdateSystem(); UpdateUser();
    UpdateAnalyser(); UpdateTour(); UpdateGateway();
    if (fileMenu.IsVisible()) {
      if (systemMenuOpen) fileMenu.BringToFront();
      else if (DataIndex(contextFile) < 0) { fileMenu.Hide(); contextFile = nullptr; }
      else {
        for (int i = UploadFile; i <= CancelFile; ++i)
          SetEnabled(Child(fileMenu.Root(), "action_" + Number(i)),
                     FileEligible(contextFile, static_cast<FileCommand>(i)));
        fileMenu.BringToFront();
      }
    }
    if (View(GatewayView).IsVisible()) View(GatewayView).BringToFront();
    if (popup.IsVisible()) popup.Focus();
  }

  bool Invoke(const std::string &command) {
    if (!PlayerModel()) return false;
    if (command == "Open System Menu") return OpenSystemMenu();
    if (command == "Reply") {
      if (popup.IsVisible() || fileMenu.IsVisible() || !EmailContext()) return false;
      Reply(); return true;
    }
    if (command == "Delete" && !popup.IsVisible() && !fileMenu.IsVisible() && EmailContext()) {
      DeleteMail(); return true;
    }
    if (command == "Copy" || command == "Decrypt" || command == "Play" ||
        command == "Delete" || command == "Delete File") {
      if (popup.IsVisible() || (fileMenu.IsVisible() && systemMenuOpen)) return false;
      Data *data = fileMenu.IsVisible() ? contextFile : FileAt(pointerX, pointerY);
      const FileCommand action = command == "Copy" ? UploadFile :
          command == "Decrypt" ? DecryptFile : command == "Play" ? PlayFile : RemoveFile;
      if (!data || !FileEligible(data, action)) return false;
      fileMenu.Hide(); contextFile = nullptr;
      return FileAction(data, action);
    }
    if (command == "Open App Menu" || command == "Toggle App Menu") return Toggle(Apps);
    if (command == "Open Files Manager" || command == "Toggle Files Manager") return Toggle(Files);
    if (command == "Open E-Mail" || command == "Toggle E-Mail") return Toggle(Email);
    if (command == "Open Status") {
      if (View(User).IsVisible() && userTab == 0) Close(User);
      else { userTab = 0; Show(User); }
      return true;
    }
    if (command == "Open Finance") {
      if (View(User).IsVisible() && userTab == 1) Close(User);
      else { userTab = 1; Show(User); }
      return true;
    }
    if (command == "Open Gateway Info" || command == "Open System") return Toggle(System);
    if (command == "Options" || command == "Open Options" || command == "Toggle Options") return Toggle(OptionsWindow);
    if (command == "Open Analyser" || command == "Open Connection Analysis") {
      if (!PlayerModel()->gateway.HasHUDUpgrade(HUDUPGRADE_CONNECTIONANALYSIS)) return false;
      return Toggle(Analyser);
    }
    if (command == "Open Tour Guide" || command == "Open Tutorial") {
      if (Task *task = SvbGetTask(const_cast<char *>("Tutorial")))
        tutorialPID = SvbLookupPID(task);
      if (!TutorialTask()) {
        if (!Launch("Tutorial", 1.0f)) return false;
      }
      tourSuppressed = false; tourMinimized = false;
      return Show(Tour);
    }
    if (command == "Open Cheats") {
#ifdef TESTGAME
      return Toggle(Cheats);
#else
      return false;
#endif
    }
    if (command == "View Gateway" || command == "Open Gateway View") return Toggle(GatewayView);
    if (command == "Compose E-Mail") {
      mailTab = 2; message = nullptr; mission = nullptr;
      if (!Show(Email)) return false;
      LoadDraft(); return true;
    }
    if (command == "Reply E-Mail") { Reply(); return true; }
    if (command == "Delete E-Mail") { DeleteMail(); return true; }
    if (command == "Save Attachment") { SaveAttachment(); return true; }
    if (command == "Send E-Mail") { if (mailTab == 2 && View(Email).IsVisible()) Send(); return true; }
    if (command == "Apply Options") { if (View(OptionsWindow).IsVisible()) ApplyOptions(); return true; }
    if (command == "Save Game" || command == "Save") { app->SaveGame(PlayerModel()->handle); return true; }
    const std::pair<const char *, Window> close[] = {{"Close App Menu", Apps}, {"Close E-Mail", Email},
      {"Close Files Manager", Files}, {"Close Options", OptionsWindow}, {"Close System", System},
      {"Close Gateway Info", System}, {"Close Status", User}, {"Close Finance", User},
      {"Close Tour Guide", Tour}, {"Close Analyser", Analyser}, {"Close Cheats", Cheats},
      {"Close Gateway View", GatewayView}};
    for (const auto &entry : close) if (command == entry.first) { Close(entry.second); return true; }
    if (command == "Close Local Windows") { SetLegacyScreen(SCREEN_NONE); Hide(); return true; }
    if (command.compare(0, 8, "Run App ") == 0 || command.compare(0, 8, "Buy App ") == 0) {
      std::istringstream input(command.substr(8));
      std::string name; float version = 1; input >> name >> version;
      return command[0] == 'R' ? Launch(name, version) : Buy(name, version);
    }
    return false;
  }
};

HDLocalWindows::HDLocalWindows(UIObject &ownerRoot) : impl(new Impl(ownerRoot)) {}
HDLocalWindows::~HDLocalWindows() { impl->Hide(); }
void HDLocalWindows::Update() { impl->Update(); }
void HDLocalWindows::Hide() { impl->Hide(); }
bool HDLocalWindows::IsVisible() const {
  if (impl->popup.IsVisible() || impl->fileMenu.IsVisible()) return true;
  for (const auto &view : impl->views) if (view->IsVisible()) return true;
  return false;
}
bool HDLocalWindows::Invoke(const std::string &command) { return impl->Invoke(command); }
bool HDLocalWindows::HandleInput(const ALLEGRO_EVENT &event) {
  if (event.type == ALLEGRO_EVENT_MOUSE_AXES ||
      event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN ||
      event.type == ALLEGRO_EVENT_MOUSE_BUTTON_UP) {
    impl->pointerX = event.mouse.x; impl->pointerY = event.mouse.y;
  }
  if ((event.type == ALLEGRO_EVENT_KEY_CHAR || event.type == ALLEGRO_EVENT_KEY_UP) &&
      event.keyboard.keycode == impl->menuDismissKey) {
    if (event.type == ALLEGRO_EVENT_KEY_UP) impl->menuDismissKey = -1;
    return true;
  }
  if (impl->popup.HandleInput(event)) return true;
  if (impl->fileMenu.IsVisible()) {
    if (event.type == ALLEGRO_EVENT_KEY_DOWN &&
        event.keyboard.keycode == ALLEGRO_KEY_ESCAPE) {
      impl->menuDismissKey = event.keyboard.keycode;
      Data *data = impl->contextFile;
      const bool cancelOperation = !impl->systemMenuOpen;
      impl->fileMenu.Hide(); impl->contextFile = nullptr; impl->systemMenuOpen = false;
      if (cancelOperation) impl->FileAction(data, Impl::CancelFile);
      return true;
    }
    if (event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN &&
        !impl->fileMenu.Root()->ContainsPoint(event.mouse.x, event.mouse.y)) {
      impl->fileMenu.Hide(); impl->contextFile = nullptr; impl->systemMenuOpen = false;
      return true;
    }
    if (event.type == ALLEGRO_EVENT_KEY_CHAR && event.keyboard.keycode == ALLEGRO_KEY_TAB)
      return true;
    if (event.type == ALLEGRO_EVENT_KEY_DOWN &&
        event.keyboard.keycode == ALLEGRO_KEY_TAB) {
      UIObject *actions = Child(impl->fileMenu.Root(), "actions");
      if (actions) {
        UIObject *focused = impl->owner.GetFocus();
        int current = -1;
        for (int i = 0; i < static_cast<int>(actions->children.size()); ++i)
          if (actions->children[i].get() == focused) { current = i; break; }
        const int count = static_cast<int>(actions->children.size());
        const int direction = event.keyboard.modifiers & ALLEGRO_KEYMOD_SHIFT ? -1 : 1;
        if (current < 0 && direction < 0) current = 0;
        for (int step = 1; step <= count; ++step) {
          const int next = (current + direction * step + count) % count;
          UIObject *button = actions->children[next].get();
          if (button->IsVisible() && button->IsEnabled()) {
            impl->owner.SetFocus(button); break;
          }
        }
      }
      return true;
    }
    switch (event.type) {
    case ALLEGRO_EVENT_MOUSE_AXES:
    case ALLEGRO_EVENT_MOUSE_BUTTON_DOWN:
    case ALLEGRO_EVENT_MOUSE_BUTTON_UP:
    case ALLEGRO_EVENT_KEY_DOWN:
    case ALLEGRO_EVENT_KEY_CHAR:
    case ALLEGRO_EVENT_KEY_UP:
      impl->owner.DispatchEvent(event);
      return true;
    default: break;
    }
  }
  if (!impl->PlayerModel()) return false;
  if (event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN && event.mouse.button == 2 &&
      impl->OpenFileMenu(event.mouse.x, event.mouse.y)) return true;
  if (event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN) {
    UIObject *hit = impl->owner.HitTest(event.mouse.x, event.mouse.y);
    for (int i = 0; i < Impl::WindowCount; ++i) {
      auto &view = impl->views[i];
      if (view->IsVisible() && hit && hit->IsDescendantOf(view->Root())) {
        view->BringToFront();
        impl->SyncLegacyWindow(static_cast<Impl::Window>(i));
        break;
      }
    }
  }
  if (impl->views[Impl::GatewayView]->IsVisible()) {
    if (event.type == ALLEGRO_EVENT_KEY_CHAR && event.keyboard.keycode == ALLEGRO_KEY_ESCAPE) {
      impl->Close(Impl::GatewayView); return true;
    }
    switch (event.type) {
    case ALLEGRO_EVENT_KEY_DOWN:
    case ALLEGRO_EVENT_KEY_UP:
    case ALLEGRO_EVENT_KEY_CHAR:
      impl->owner.SetFocus(Child(impl->Root(Impl::GatewayView), "controls/btnClose"));
      impl->owner.DispatchEvent(event);
      return true;
    case ALLEGRO_EVENT_MOUSE_AXES:
    case ALLEGRO_EVENT_MOUSE_BUTTON_DOWN:
    case ALLEGRO_EVENT_MOUSE_BUTTON_UP:
      impl->owner.DispatchEvent(event);
      return true;
    default: break;
    }
  }
  return false;
}

} // namespace HDUI
