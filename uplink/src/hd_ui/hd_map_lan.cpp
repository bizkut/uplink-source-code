#include "hd_map_lan.h"
#include "hd_legacy_bridge.h"
#include "hd_app_windows.h"
#include "app/app.h"
#include "app/globals.h"
#include "game/game.h"
#include "game/data/data.h"
#include "interface/interface.h"
#include "interface/localinterface/localinterface.h"
#include "interface/localinterface/lan_interface.h"
#include "interface/localinterface/worldmap/worldmap_interface.h"
#include "interface/remoteinterface/remoteinterface.h"
#include "interface/taskmanager/taskmanager.h"
#include "interface/taskmanager/lanprobe.h"
#include "interface/taskmanager/lanscan.h"
#include "interface/taskmanager/lanspoof.h"
#include "interface/taskmanager/lanforce.h"
#include "world/world.h"
#include "world/player.h"
#include "world/message.h"
#include "world/company/mission.h"
#include "world/computer/lancomputer.h"
#include "world/computer/lanmonitor.h"
#include "soundgarden.h"
#include "redshirt.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <utility>
#include <vector>

namespace HDUI {
namespace {
Player *CurrentPlayer() {
  return app && !app->Closed() && game && game->GameSpeed() >= 0 &&
                 LegacyHasButton("hud_mainmenu")
             ? game->GetWorld()->GetPlayer() : nullptr;
}
LanComputer *CurrentLAN() {
  Player *player = CurrentPlayer();
  VLocation *host = player ? player->GetRemoteHost() : nullptr;
  Computer *computer = host ? host->GetComputer() : nullptr;
  return computer && computer->TYPE == COMPUTER_TYPE_LAN
             ? dynamic_cast<LanComputer *>(computer) : nullptr;
}
std::string AssetPath(const char *relative) {
  std::string path = app ? app->path : "";
  if (!path.empty() && path.back() != '/' && path.back() != '\\') path += '/';
  return path + relative;
}
void PlaySound(const char *relative) {
  char *name = const_cast<char *>(relative);
  SgPlaySound(RsArchiveFileOpen(name), name, true);
}
UIObject *Child(UIObject *root, const char *name) {
  return root ? root->FindChildRecursive(name) : nullptr;
}
void ShowChild(UIObject *root, const char *name, bool visible) {
  SetVisible(Child(root, name), visible);
}
void SetObjectColor(UIObject *object, const std::string &color) {
  if (auto *rectangle = dynamic_cast<UIRectangle *>(object)) {
    rectangle->fillColor = color;
    rectangle->strokeColor = color;
  }
  if (auto *text = dynamic_cast<UITextPoint *>(object)) text->fillColor = color;
  if (auto *line = dynamic_cast<UILine *>(object)) line->strokeColor = color;
}
std::string ReferenceColor(UIObject *root, const char *name, const char *fallback) {
  auto *reference = dynamic_cast<UIRectangle *>(Child(root, name));
  return reference ? reference->fillColor : fallback;
}
void SetIcon(UIObject *root, const char *filename) {
  if (auto *image = dynamic_cast<UIImage *>(Child(root, "icon"))) {
    if (image->file != filename) {
      image->file = filename;
      image->w = image->h = 0;
      image->LoadBitmap(AssetPath("uplinkHD/graphics"));
    }
  }
}
const char *LANIcon(const LanComputerSystem &system) {
  switch (system.TYPE) {
  case LANSYSTEM_ROUTER: return "lanEl_icon_router.png";
  case LANSYSTEM_HUB: case LANSYSTEM_SWITCH: case LANSYSTEM_FAXPRINTER: return "lanEl_icon_hub.png";
  case LANSYSTEM_TERMINAL: return "lanEl_icon_terminal.png";
  case LANSYSTEM_MAINSERVER: case LANSYSTEM_SESSIONKEYSERVER: return "lanEl_icon_mainServer.png";
  case LANSYSTEM_MAILSERVER: return "lanEl_icon_mailServer.png";
  case LANSYSTEM_FILESERVER: return "lanEl_icon_fileServer.png";
  case LANSYSTEM_AUTHENTICATION: return "lanEl_icon_authenticationServer.png";
  case LANSYSTEM_LOCK: return system.data1 ? "lanEl_icon_lock.png" : "lanEl_icon_lock02.png";
  case LANSYSTEM_ISOLATIONBRIDGE: return "lanEl_icon_isolationBridge.png";
  case LANSYSTEM_MODEM: return "lanEl_icon_modem.png";
  case LANSYSTEM_RADIOTRANSMITTER: return "lanEl_icon_wirelessTransmitter.png";
  case LANSYSTEM_RADIORECEIVER: return "lanEl_icon_wirelessReceiver.png";
  case LANSYSTEM_LOGSERVER: return "lanEl_icon_mailServer.png";
  default: return "lanEl_icon_hub.png";
  }
}
const char *LANRelation(LanComputer *computer, int index, int selected) {
  if (!computer->systems.ValidIndex(index) || !computer->systems.ValidIndex(selected)) return "";
  LanComputerSystem *system = computer->systems.GetData(index);
  LanComputerSystem *source = computer->systems.GetData(selected);
  if (!source || !system || source->visible < LANSYSTEMVISIBLE_TYPE) return "";
  if (source->TYPE == LANSYSTEM_AUTHENTICATION &&
      (source->data1 == index || source->data2 == index || source->data3 == index))
    return "Locks/Unlocks";
  if (source->TYPE == LANSYSTEM_ISOLATIONBRIDGE) {
    if (source->data1 == index) return "Locks";
    if (source->data2 == index) return "Unlocks";
  }
  if (source->TYPE == LANSYSTEM_LOCK &&
      (system->TYPE == LANSYSTEM_AUTHENTICATION || system->TYPE == LANSYSTEM_ISOLATIONBRIDGE) &&
      (system->data1 == selected || system->data2 == selected || system->data3 == selected))
    return "Controller";
  if (source->TYPE == LANSYSTEM_MAINSERVER &&
      (source->data1 == index || source->data2 == index || source->data3 == index))
    return "GuardedBy";
  for (int i = 0; i < source->validSubnets.Size(); ++i)
    if (source->validSubnets.ValidIndex(i) && source->validSubnets.GetData(i) == index)
      return "Valid Subnet";
  return "";
}
void FitText(UITextPoint *text, float width) {
  if (!text) return;
  text->PrepareText();
  if (text->Measure(0, text->text.size()) <= width) return;
  std::string value = text->text;
  do {
    if (value.empty()) break;
    size_t end = value.size() - 1;
    while (end > 0 && (static_cast<unsigned char>(value[end]) & 0xC0) == 0x80) --end;
    value.resize(end);
    text->text = value + "...";
  } while (text->Measure(0, text->text.size()) > width);
}
std::unique_ptr<UIButtonStandard> MenuButton(const std::string &command, float y,
                                            float width) {
  std::unique_ptr<UIButtonStandard> button(new UIButtonStandard());
  button->name = command;
  button->x = 7; button->y = y; button->w = width - 14; button->h = 30;
  for (int i = 0; i != 2; ++i) {
    std::unique_ptr<UIRectangle> graphic(new UIRectangle());
    graphic->name = i ? "gfxClick" : "gfxHover";
    graphic->w = button->w; graphic->h = 30;
    graphic->fillColor = "2BAAFF";
    graphic->fillAlpha = i ? 1.0f : 0.25f;
    button->AddChild(std::move(graphic));
  }
  std::unique_ptr<UITextPoint> caption(new UITextPoint());
  caption->name = "txtCaption"; caption->text = command;
  caption->x = 8; caption->y = 1; caption->font = "AeroMaticsLightRegular";
  caption->fontSize = 24; caption->fillColor = "2BAAFF";
  caption->assetBasePath = AssetPath("uplinkHD/fonts");
  button->AddChild(std::move(caption));
  return button;
}
} // namespace

struct HDMapLan::State {
  struct Row { UIObject *root = nullptr; unsigned generation = 0; };
  struct LANRow : Row { double doneUntil = 0; bool wasDone = false; };
  UIObject &owner;
  HDLayoutView map, lan, browser, notification, menu;
  HDPopupController popup;
  HDRepeatLayoutFactory mapElements, lanElements;
  std::map<std::string, Row> locations, routeLines;
  std::map<int, LANRow> systems;
  std::map<int, Row> lanLinks;
  std::map<int, int> scanTargets;
  Player *player = nullptr;
  Player *sessionPlayer = nullptr;
  LanComputer *lanComputer = nullptr;
  std::string contextIP, pendingIP, remoteIP;
  int contextSystem = -1;
  int swallowedKey = -1;
  unsigned generation = 0;
  int unreadMail = 0, unreadMissions = 0;
  UplinkObject *notificationObject = nullptr;
  bool notificationIsMission = false, mapBound = false, lanBound = false;
  bool notificationBound = false;
  double notificationStart = 0, browserStart = 0;

  explicit State(UIObject &root)
      : owner(root), map(root, "HUD/WorldMap"), lan(root, "HUD/windows/WinLAN"),
        browser(root, "HUD/Browser"), notification(root, "HUD/EmailNotification"),
        menu(root, "Misc/ButtonsMenuReference"), popup(root),
        mapElements("HUD/WorldMapElement"), lanElements("HUD/windows/WinLANElement") {
    map.SetCaptureInput(true);
  }
  void Bind(HDLayoutView &view, const char *path, const std::string &command) {
    if (UIObject *button = view.Find(path))
      button->onClickCallback = [this, command](UIObject *) { Invoke(command); };
  }
  void RaiseWindow(HDLayoutView &view) {
    UIObject *root = view.Root();
    if (!root) return;
    root->interactive = true;
    HDLayoutView *window = &view;
    root->onClickCallback = [window](UIObject *) { window->BringToFront(); };
  }
  bool ShowMap() {
    if (!CurrentPlayer()) return false;
    if (!map.Root()) { locations.clear(); routeLines.clear(); mapBound = false; }
    if (!map.Show()) return false;
    game->GetInterface()->GetLocalInterface()->RunScreen(SCREEN_NONE);
    if (!mapBound) {
      Bind(map, "menu/btnConnect", "Map Connect");
      Bind(map, "menu/btnReset", "Map Reset");
      Bind(map, "menu/btnLoad", "Map Load");
      Bind(map, "menu/btnSave", "Map Save");
      Bind(map, "menu/btnRandomize", "Map Randomize");
      Bind(map, "menu/btnClose", "Close World Map");
      mapBound = true;
    }
    WorldMapInterface::CreateWorldMapInterface(WORLDMAP_LARGE);
    RaiseWindow(map);
    return true;
  }
  void CloseMap() {
    menu.Hide(); map.Hide();
    contextIP.clear();
    if (CurrentPlayer() && WorldMapInterface::IsVisibleWorldMapInterface() == WORLDMAP_LARGE)
      WorldMapInterface::CreateWorldMapInterface(WORLDMAP_SMALL);
  }
  bool ShowLAN() {
    if (!CurrentPlayer() ||
        !CurrentPlayer()->gateway.HasHUDUpgrade(HUDUPGRADE_LANVIEW))
      return false;
    LocalInterface *localInterface = game->GetInterface()->GetLocalInterface();
    if (localInterface->InScreen() != SCREEN_LAN) localInterface->RunScreen(SCREEN_LAN);
    if (localInterface->InScreen() != SCREEN_LAN) return false;
    if (!lan.Root()) { systems.clear(); lanLinks.clear(); lanBound = false; lanComputer = nullptr; }
    if (!lan.Show()) return false;
    lan.Root()->x = 958; lan.Root()->y = -1;
    if (!lanBound) {
      Bind(lan, "controls/btnReset", "LAN Reset");
      Bind(lan, "controls/btnConnect", "LAN Connect");
      Bind(lan, "sidePanel/btnClose", "Close LAN View");
      for (const char *name : {"lineNormal", "lineConnected", "lineTrace"})
        SetVisible(lan.Find(name), false);
      if (UIObject *network = lan.Find("network")) network->clipChildren = true;
      if (auto *canvas = lan.FindAs<UIDraggable>("network/elements")) {
        canvas->scalingType = ScalingType::ScaleNormal;
        canvas->bounded = true;
      }
      lanBound = true;
    }
    RaiseWindow(lan);
    return true;
  }
  bool Editable() {
    if (!CurrentPlayer()) return false;
    if (!player->IsConnected() && !LegacyHasButton("dialler_number")) return true;
    popup.ShowInfo("World Map", "You cannot modify an existing connection. Disconnect first.");
    return false;
  }
  bool Protected(VLocation *location) const {
    return !location || !std::strcmp(location->ip, player->localhost) ||
           !std::strcmp(location->ip, IP_INTERNIC);
  }
  VLocation *ContextLocation() {
    if (!player) return nullptr;
    const char *ip = contextIP.empty() ? player->remotehost : contextIP.c_str();
    return game->GetWorld()->GetVLocation(const_cast<char *>(ip));
  }
  void EditRoute(const std::string &ip) {
    if (!Editable()) return;
    VLocation *location = game->GetWorld()->GetVLocation(const_cast<char *>(ip.c_str()));
    if (!location || (!player->HasLink(location->ip) &&
                      !player->connection.LocationIncluded(location->ip))) return;
    player->connection.AddOrRemoveVLocation(location->ip);
    contextIP = ip;
  }
  void Disconnect() {
    Connection &connection = player->connection;
    if (player->IsConnected()) connection.Disconnect();
    connection.Reset();
    pendingIP.clear(); browser.Hide();
    game->GetInterface()->GetRemoteInterface()->RunNewLocation();
    game->GetInterface()->GetRemoteInterface()->RunScreen(1);
  }
  bool Connect() {
    if (player->IsConnected()) return false;
    if (LegacyHasButton("dialler_number")) return true;
    Connection &connection = player->connection;
    char *target = connection.GetTarget();
    if (!target || connection.GetSize() < 2 || !std::strcmp(target, player->localhost)) return false;
    VLocation *location = game->GetWorld()->GetVLocation(target);
    if (!location || !location->GetComputer()) return false;
    pendingIP = target;
    connection.Connect();
    game->GetInterface()->GetRemoteInterface()->RunNewLocation();
    browserStart = al_get_time();
    CloseMap();
    return true;
  }
  bool HasRandomizer() const {
    return player && player->gateway.HasHUDUpgrade(HUDUPGRADE_CONNECTIONRANDOMIZER);
  }
  void Randomize() {
    if (!Editable()) return;
    if (!HasRandomizer()) {
      popup.ShowInfo("World Map", "The Connection Randomizer HUD upgrade is not installed.");
      return;
    }
    std::vector<std::string> candidates, selected;
    Connection &connection = player->connection;
    for (int i = 1; i < connection.GetSize(); ++i) {
      char *ip = connection.vlocations.GetData(i);
      if (ip && player->HasLink(ip) && std::strcmp(ip, IP_INTERNIC))
        selected.emplace_back(ip);
    }
    for (int i = 0; i < player->links.Size(); ++i) {
      char *ip = player->links.GetData(i);
      VLocation *location = ip ? game->GetWorld()->GetVLocation(ip) : nullptr;
      if (location && location->GetComputer() && std::strcmp(ip, player->localhost) &&
          std::strcmp(ip, IP_INTERNIC) && !connection.LocationIncluded(ip))
        candidates.emplace_back(ip);
    }
    for (size_t i = candidates.size(); i > 1; --i)
      std::swap(candidates[i - 1], candidates[static_cast<size_t>(std::rand()) % i]);
    connection.Reset();
    char internic[] = IP_INTERNIC;
    if (player->HasLink(internic) && game->GetWorld()->GetVLocation(internic))
      connection.AddVLocation(internic);
    for (std::string &ip : candidates) connection.AddVLocation(&ip[0]);
    for (std::string &ip : selected)
      if (game->GetWorld()->GetVLocation(&ip[0])) connection.AddVLocation(&ip[0]);
  }
  bool LinkCommand(const std::string &command) {
    VLocation *location = ContextLocation();
    if (!location || !std::strcmp(location->ip, player->localhost)) return false;
    if (command == "Bookmark") {
      if (player->HasLink(location->ip)) {
        if (Protected(location)) {
          popup.ShowInfo("Bookmark", "This link cannot be removed.");
          return true;
        }
        player->RemoveLink(location->ip);
      } else player->GiveLink(location->ip);
    } else if (command == "Favourite") {
      if (!player->HasLink(location->ip)) player->GiveLink(location->ip);
      location->SetColorDisplayed(!location->colored);
      if (location->colored) location->SetDisplayed(true);
    } else {
      if (Protected(location) && location->displayed) {
        popup.ShowInfo("World Map", "This link must remain visible.");
        return true;
      }
      if (!player->HasLink(location->ip)) player->GiveLink(location->ip);
      location->SetDisplayed(!location->displayed);
      if (!location->displayed) location->SetColorDisplayed(false);
    }
    return true;
  }
  bool SelectSystem(int index) {
    LanComputer *computer = CurrentLAN();
    if (!computer || !computer->systems.ValidIndex(index)) return false;
    LanComputerSystem *system = computer->systems.GetData(index);
    if (!system || system->visible == LANSYSTEMVISIBLE_NONE) return false;
    contextSystem = index;
    const std::string button = "lansystem " + std::to_string(index);
    // This callback also supplies a real Eclipse target to legacy LAN tools.
    if (LegacyClick(button.c_str())) return true;
    if (system->visible > LANSYSTEMVISIBLE_AWARE) {
      LanMonitor::SetCurrentSelected(index);
      if (!LanMonitor::IsInConnection(index)) LanMonitor::ExtendConnection(index);
    }
    return true;
  }
  bool LANTool(const std::string &command) {
    LanComputer *computer = CurrentLAN();
    if (!computer) return false;
    if (!lan.IsVisible() && !ShowLAN()) return false;
    const char *software = command == "LAN Probe" ? "LAN_Probe" :
                           command == "LAN Scan" ? "LAN_Scan" :
                           command == "LAN Spoof" ? "LAN_Spoof" : "LAN_Force";
    Data *installed = nullptr;
    DataBank &storage = player->gateway.databank;
    for (int i = 0; i < storage.GetDataSize(); ++i) {
      Data *file = storage.GetDataFile(i);
      if (file && !std::strcmp(file->title, software) && file->TYPE == DATATYPE_PROGRAM &&
          !file->encrypted && !file->compressed && (!installed || file->version > installed->version))
        installed = file;
    }
    if (!installed) {
      popup.ShowInfo(command, "This program is not installed or is not executable.");
      return true;
    }
    const int selected = contextSystem >= 0 ? contextSystem : LanMonitor::currentSelected;
    LanComputerSystem *system = computer->systems.ValidIndex(selected) ? computer->systems.GetData(selected) : nullptr;
    if (command != "LAN Scan" && (!system || system->visible == LANSYSTEMVISIBLE_NONE)) return false;
    if (command == "LAN Force" && system->TYPE != LANSYSTEM_LOCK) {
      popup.ShowInfo("ERROR", "LAN_Force can only be used on a Lock.");
      return true;
    }
    if ((command == "LAN Force" || command == "LAN Spoof") && system->security > installed->version) {
      popup.ShowInfo("ERROR", command == "LAN Force"
        ? "You need a higher version of LAN_Force to successfully force open this Lock."
        : "You need a higher version of LAN_Spoof to successfully spoof this System.");
      return true;
    }
    for (int i = 0; i < SvbNumTasks(); ++i) {
      TaskWrapper *wrapper = SvbGetTaskWrapperAtIndex(i);
      if (!wrapper) continue;
      Task *task = SvbGetTask(wrapper->pid);
      bool busy = false;
      if (auto *probe = dynamic_cast<LanProbe *>(task))
        busy = probe->GetLANComputer() == computer && probe->GetSystemIndex() == selected &&
               probe->GetStatus() != LANPROBE_FINISHED;
      else if (auto *spoof = dynamic_cast<LanSpoof *>(task))
        busy = spoof->GetLANComputer() == computer && spoof->GetSystemIndex() == selected &&
               spoof->GetStatus() != LANSPOOF_FAILED && spoof->GetStatus() != LANSPOOF_SPOOFED;
      else if (auto *force = dynamic_cast<LanForce *>(task))
        busy = force->GetLANComputer() == computer && force->GetSystemIndex() == selected &&
               force->GetStatus() != LANFORCE_FINISHED;
      else if (auto *scan = dynamic_cast<LanScan *>(task))
        busy = command == "LAN Scan" && scan->GetStatus() == LANSCAN_SCANNING;
      if (busy) {
        popup.ShowInfo("ERROR", "An App is already using this System. Please wait for the other App to finish and then try again.");
        return true;
      }
    }
    TaskManager *tasks = game->GetInterface()->GetTaskManager();
    const int pid = LaunchOwnedApp(software);
    if (pid < 0) {
      popup.ShowInfo("WARNING", std::string("You're running too many instances of ") + software +
        ". Please wait for the others to complete and then try again.");
      return true;
    }
    UplinkTask *task = dynamic_cast<UplinkTask *>(SvbGetTask(pid));
    if (!task) return false;
    task->ShowInterface();
    if (command == "LAN Scan") {
      const std::string go = "lanscan_title " + std::to_string(pid);
      const bool started = LegacyClick(go.c_str());
      if (started) scanTargets[pid] = selected;
      return started;
    }
    if (selected < 0 || !computer->systems.ValidIndex(selected)) return true;
    const std::string target = "lansystem " + std::to_string(selected);
    if (LegacyHasButton(target.c_str()))
      tasks->SetProgramTarget(computer, const_cast<char *>(target.c_str()), selected);
    return true;
  }
  bool Invoke(const std::string &command) {
    player = CurrentPlayer();
    if (!player) return false;
    const bool contextMenuVisible = menu.IsVisible();
    if (contextMenuVisible) menu.Hide();
    if (command == "Open World Map" || command == "Toggle World Map" || command == "World Map") {
      if (map.IsVisible()) return Invoke("Close World Map");
      return ShowMap();
    }
    if (command == "Close World Map") {
      if (!player->IsConnected()) player->connection.Reset();
      CloseMap(); return true;
    }
    if (command == "Open LAN View") {
      if (lan.IsVisible()) {
        lan.Hide(); game->GetInterface()->GetLocalInterface()->RunScreen(SCREEN_NONE);
      } else return ShowLAN();
      return true;
    }
    if (command == "Close LAN View") {
      menu.Hide(); lan.Hide(); game->GetInterface()->GetLocalInterface()->RunScreen(SCREEN_NONE);
      return true;
    }
    if (command == "Show On Map" || command == "Favourite" || command == "Bookmark") {
      if (!map.IsVisible() && !contextMenuVisible) return false;
      return LinkCommand(command);
    }
    if (command == "Map Connect" || command == "Connect") return Connect();
    if (command == "Disconnect") { if (!player->IsConnected()) return false; Disconnect(); return true; }
    if (command == "Map Reset") {
      if (player->IsConnected()) Disconnect();
      else if (Editable()) player->connection.Reset();
      return true;
    }
    if (command == "Map Randomize") { Randomize(); return true; }
    if (command == "Map Save") {
      if (!LegacyHasButton("worldmap_saveconnection")) WorldMapInterface::CreateWorldMapInterface(WORLDMAP_LARGE);
      return LegacyClick("worldmap_saveconnection");
    }
    if (command == "Map Load") {
      if (!Editable()) return true;
      if (!LegacyHasButton("worldmap_loadconnection")) WorldMapInterface::CreateWorldMapInterface(WORLDMAP_LARGE);
      if (!LegacyClick("worldmap_loadconnection")) return false;
      // Saved routes can outlive deleted bookmarks; loading must not discover
      // servers the player no longer owns. The source entry is mandatory.
      for (int i = player->connection.GetSize() - 1; i > 0; --i) {
        char *ip = player->connection.vlocations.GetData(i);
        if (!player->HasLink(ip) || !game->GetWorld()->GetVLocation(ip))
          player->connection.AddOrRemoveVLocation(ip);
      }
      return true;
    }
    if (command == "LAN Connect" || command == "LAN Reset") {
      LanComputer *computer = CurrentLAN();
      if (!computer) return false;
      if (!lan.IsVisible() && !ShowLAN()) return false;
      if (command == "LAN Connect" && contextSystem >= 0) {
        if (!computer->systems.ValidIndex(contextSystem)) return false;
        LanComputerSystem *system = computer->systems.GetData(contextSystem);
        if (!system || system->visible <= LANSYSTEMVISIBLE_AWARE) return false;
        SelectSystem(contextSystem);
      }
      return LegacyClick(command == "LAN Connect" ? "lan_connect" : "lan_cancel");
    }
    if (command == "LAN Probe" || command == "LAN Scan" || command == "LAN Spoof" || command == "LAN Force")
      return LANTool(command);
    return false;
  }

  void OpenMenu(float px, float py, bool lanContext) {
    if (!menu.Load()) return;
    UIObject *root = menu.Root();
    // Rebuild only on a new context invocation, never while a menu callback is
    // executing. Hide retains the entire callback-owning tree until then.
    UIObject *entries = root->FindChild("entries");
    if (!entries) {
      std::unique_ptr<UIContainer> container(new UIContainer());
      container->name = "entries"; entries = container.get();
      root->AddChild(std::move(container));
    }
    entries->ClearChildren();
    const std::vector<std::string> commands = lanContext
      ? std::vector<std::string>{"LAN Probe", "LAN Scan", "LAN Spoof", "LAN Force", "LAN Connect", "LAN Reset"}
      : std::vector<std::string>{"Show On Map", "Favourite", "Bookmark", "Map Connect", "Map Reset", "Map Randomize", "Map Save", "Map Load"};
    root->w = 230; root->h = 12 + 30 * commands.size();
    entries->w = root->w; entries->h = root->h;
    if (UIObject *background = root->FindChild("baseGFX")) {
      background->w = root->w; background->h = root->h - 8;
    }
    if (auto *top = dynamic_cast<UILine *>(root->FindChild("anchorTop"))) {
      top->w = root->w; top->x2 = root->w;
    }
    if (auto *bottom = dynamic_cast<UILine *>(root->FindChild("anchorBottom"))) {
      bottom->y = root->h - 4; bottom->w = root->w; bottom->x2 = root->w;
    }
    for (const char *reference : {"separatorRef", "btnBaseHoverRef", "btnBaseClickRef", "btnCaptionRef"})
      ShowChild(root, reference, false);
    float y = 6;
    for (const std::string &command : commands) {
      auto button = MenuButton(command, y, root->w);
      button->onClickCallback = [this, command](UIObject *) { menu.Hide(); Invoke(command); };
      entries->AddChild(std::move(button));
      y += 30;
    }
    const Geometry geometry = owner.GetGeometry();
    root->x = std::max(0.0f, std::min(1920.0f - root->w, (px - geometry.x) / geometry.sx));
    root->y = std::max(0.0f, std::min(1080.0f - root->h, (py - geometry.y) / geometry.sy));
    root->interactive = true;
    menu.Show();
    if (!entries->children.empty()) owner.SetFocus(entries->children.front().get());
  }

  void MapRow(VLocation *location, int routeIndex) {
    if (!location) return;
    UIObject *container = map.Find("linksContainer");
    if (!container) return;
    Row &row = locations[location->ip];
    if (!row.root) {
      std::unique_ptr<UIObject> tree = mapElements.Create();
      if (!tree) { locations.erase(location->ip); return; }
      row.root = tree.get();
      tree->name = std::string("location:") + location->ip;
      for (const char *reference : {"cNorm_ref", "cTarget_ref", "cFav_ref", "cFavTarget_ref", "cTrace_ref"})
        ShowChild(tree.get(), reference, false);
      const std::string ip = location->ip;
      if (UIObject *button = Child(tree.get(), "btnLink")) {
        button->onClickCallback = [this, ip](UIObject *) { EditRoute(ip); };
        button->onHoverCallback = [this, ip](UIObject *) { contextIP = ip; };
      }
      container->AddChild(std::move(tree));
    }
    row.generation = generation;
    // GOLD sub_46D9F0 supplies raw VLocation coordinates; the map doubles
    // those coordinates before the element's ten-pixel center offset.
    row.root->x = location->x * 2.0f - 10;
    row.root->y = location->y * 2.0f - 10;
    const bool target = player->HasMissionLink(location->ip) || player->HasMessageLink(location->ip);
    const char *colorName = location->colored
      ? (target ? "cFavTarget_ref" : "cFav_ref") : target ? "cTarget_ref" : "cNorm_ref";
    const std::string color = ReferenceColor(row.root, colorName, "FFFFFF");
    SetObjectColor(Child(row.root, "gfxLink"), color);
    SetObjectColor(Child(row.root, "txtLink"), color);
    ShowChild(row.root, "gfxHasAccess", player->HasAccount(location->ip) > 0);
    SetText(Child(row.root, "txtLink"), location->computer);
    auto *text = dynamic_cast<UITextPoint *>(Child(row.root, "txtLink"));
    UIObject *background = Child(row.root, "txtBg");
    if (text && background) background->w = text->Measure(0, text->text.size()) + 10;
    UIObject *button = Child(row.root, "btnLink");
    const bool showLabel = location->colored || target || routeIndex >= 0 ||
                          (button && button->isHovered);
    ShowChild(row.root, "txtLink", showLabel);
    ShowChild(row.root, "txtBg", showLabel);
  }
  void UpdateMap() {
    if (!map.IsVisible()) return;
    ++generation;
    Connection &connection = player->connection;
    for (int i = 0; i < player->links.Size(); ++i) {
      char *ip = player->links.GetData(i);
      VLocation *location = ip ? game->GetWorld()->GetVLocation(ip) : nullptr;
      if (location && location->displayed && std::strcmp(location->ip, player->localhost)) MapRow(location, -1);
    }
    for (int i = 0; i < connection.GetSize(); ++i) {
      VLocation *location = game->GetWorld()->GetVLocation(connection.vlocations.GetData(i));
      if (i == 0 || !location) continue;
      MapRow(location, i);
      VLocation *previous = game->GetWorld()->GetVLocation(connection.vlocations.GetData(i - 1));
      if (!previous) continue;
      Row &row = routeLines[location->ip];
      if (!row.root) {
        std::unique_ptr<UILine> line(new UILine());
        line->name = std::string("route:") + location->ip;
        line->strokeWidth = 2; line->zOrder = -1;
        row.root = line.get();
        map.Find("linksContainer")->AddChild(std::move(line));
      }
      row.generation = generation;
      auto *line = static_cast<UILine *>(row.root);
      Row &to = locations[location->ip];
      if (!to.root) continue;
      if (i == 1) {
        UIObject *gateway = map.Find("linksContainer/iconGW");
        line->x1 = gateway->x + gateway->w * 0.5f;
        line->y1 = gateway->y + gateway->h * 0.5f;
      } else {
        auto from = locations.find(previous->ip);
        if (from == locations.end() || !from->second.root) continue;
        line->x1 = from->second.root->x + 10; line->y1 = from->second.root->y + 10;
      }
      line->x2 = to.root->x + 10; line->y2 = to.root->y + 10;
      const bool traced = connection.TraceInProgress() && player->gateway.HasHUDUpgrade(HUDUPGRADE_MAPSHOWSTRACE) &&
        connection.traceprogress == connection.GetSize() - i - 1;
      line->strokeColor = traced ? "D31A1A" : player->IsConnected() ? "FFFFFF" : "2BAAFF";
    }
    Retire(locations); Retire(routeLines);
    SetText(map.Find("menu/btnReset"), player->IsConnected() ? "Disconnect" : "Reset");
    const bool editable = !player->IsConnected() && !LegacyHasButton("dialler_number");
    SetEnabled(map.Find("menu/btnConnect"), editable && connection.GetSize() > 1);
    SetEnabled(map.Find("menu/btnReset"), !LegacyHasButton("dialler_number"));
    SetEnabled(map.Find("menu/btnLoad"), editable);
    SetVisible(map.Find("menu/btnRandomize"), HasRandomizer());
    SetEnabled(map.Find("menu/btnRandomize"), editable && HasRandomizer());
  }
  template <class Key, class Value> void Retire(std::map<Key, Value> &rows) {
    for (auto it = rows.begin(); it != rows.end();) {
      if (it->second.generation == generation) { ++it; continue; }
      UIObject *root = it->second.root;
      if (root && root->parent) root->parent->DetachChild(root);
      it = rows.erase(it);
    }
  }

  void LANProgress(LANRow &row, int index, LanComputer *computer) {
    bool running = false, done = false;
    float progress = 0;
    for (int i = 0; i < SvbNumTasks(); ++i) {
      TaskWrapper *wrapper = SvbGetTaskWrapperAtIndex(i);
      if (!wrapper) continue;
      Task *task = SvbGetTask(wrapper->pid);
      int status = 0, ticks = 0, current = 0;
      bool matches = false, active = false;
      if (auto *probe = dynamic_cast<LanProbe *>(task)) {
        matches = probe->GetLANComputer() == computer && probe->GetSystemIndex() == index;
        status = probe->GetStatus(); ticks = probe->GetTicksRequired(); current = probe->GetProgress();
        done = done || (matches && status == LANPROBE_FINISHED);
        active = status == LANPROBE_SCANNINGSYSTEM || status == LANPROBE_SCANNINGLINKS;
      } else if (auto *spoof = dynamic_cast<LanSpoof *>(task)) {
        matches = spoof->GetLANComputer() == computer && spoof->GetSystemIndex() == index;
        status = spoof->GetStatus(); ticks = spoof->GetTicksRequired(); current = spoof->GetProgress();
        done = done || (matches && status == LANSPOOF_SPOOFED);
        active = status == LANSPOOF_SPOOFING;
      } else if (auto *force = dynamic_cast<LanForce *>(task)) {
        matches = force->GetLANComputer() == computer && force->GetSystemIndex() == index;
        status = force->GetStatus(); ticks = force->GetTicksRequired(); current = force->GetProgress();
        done = done || (matches && status == LANFORCE_FINISHED);
        active = status == LANFORCE_FORCING;
      } else if (auto *scan = dynamic_cast<LanScan *>(task)) {
        const auto target = scanTargets.find(wrapper->pid);
        const int system = target == scanTargets.end() ? LanMonitor::currentSystem : target->second;
        matches = index == system && !std::strcmp(scan->GetScanIP(), player->remotehost);
        status = scan->GetStatus(); ticks = scan->GetTicksRequired(); current = scan->GetProgress();
        done = done || (matches && status == LANSCAN_FINISHED);
        active = status == LANSCAN_SCANNING;
      }
      if (matches && ticks > 0 && active) {
        running = true;
        progress = std::max(progress, std::min(1.0f, current / static_cast<float>(ticks)));
      }
    }
    if (done && !row.wasDone) {
      row.doneUntil = al_get_time() + 3.0;
      PlaySound("uplinkHD/sounds/success.wav");
    }
    row.wasDone = done;
    ShowChild(row.root, "appCharge", running);
    ShowChild(row.root, "appDone", !running && al_get_time() < row.doneUntil);
    if (auto *arc = dynamic_cast<UIArc *>(Child(row.root, "appCharge")))
      arc->endAngle = arc->startAngle + 360.0f * progress;
  }
  void UpdateLAN() {
    if (!lan.IsVisible()) return;
    LanComputer *computer = CurrentLAN();
    auto *canvas = lan.FindAs<UIDraggable>("network/elements");
    if (!canvas) return;
    if (computer != lanComputer) {
      canvas->ClearChildren(); systems.clear(); lanLinks.clear();
      canvas->x = canvas->y = 0; contextSystem = -1;
      lanComputer = computer;
    }
    ++generation;
    if (!computer) {
      SetText(lan.Find("sidePanel/txtTitle"), "Local Area Network");
      SetText(lan.Find("sidePanel/txtDesc"), "No Local Area Network (LAN) detected.");
      SetText(lan.Find("txtAdminStatus"), "Admin Status: Offline");
      SetEnabled(lan.Find("controls/btnConnect"), false);
      SetEnabled(lan.Find("controls/btnReset"), false);
      return;
    }
    if (!menu.IsVisible()) contextSystem = LanMonitor::currentSelected;
    float minX = 0, minY = 0, maxX = 90, maxY = 94;
    for (int index = 0; index < computer->systems.Size(); ++index) {
      if (!computer->systems.ValidIndex(index)) continue;
      LanComputerSystem *system = computer->systems.GetData(index);
      if (!system || system->visible == LANSYSTEMVISIBLE_NONE) continue;
      LANRow &row = systems[index];
      if (!row.root) {
        auto tree = lanElements.Create();
        if (!tree) { systems.erase(index); continue; }
        row.root = tree.get(); tree->name = "system:" + std::to_string(index);
        for (const char *name : {"btnBase01", "btnBase02"})
          if (UIObject *button = Child(tree.get(), name))
            button->onClickCallback = [this, index](UIObject *button) {
              if (SelectSystem(index))
                OpenMenu(button->GetAbsoluteX(), button->GetAbsoluteY(), true);
            };
        canvas->AddChild(std::move(tree));
      }
      row.generation = generation;
      row.root->x = system->x * 2.0f; row.root->y = system->y * 2.0f;
      minX = std::min(minX, row.root->x); minY = std::min(minY, row.root->y);
      maxX = std::max(maxX, row.root->x + 180); maxY = std::max(maxY, row.root->y + 94);
      const bool connected = index == LanMonitor::currentSystem || index == LanMonitor::currentSelected;
      const char *relation = LANRelation(computer, index, LanMonitor::currentSelected);
      ShowChild(row.root, "btnBase01", !connected);
      ShowChild(row.root, "btnBase02", connected);
      ShowChild(row.root, "signal01", relation[0] != 0);
      ShowChild(row.root, "signal02", index == LanMonitor::sysAdminCurrentSystem && LanMonitor::sysAdminState >= SYSADMIN_SEARCHING);
      ShowChild(row.root, "signal03", index == LanMonitor::currentSystem);
      ShowChild(row.root, "selection", index == LanMonitor::currentSelected);
      ShowChild(row.root, "icon", system->visible >= LANSYSTEMVISIBLE_TYPE);
      SetIcon(row.root, LANIcon(*system));
      SetText(Child(row.root, "txtLabel"), relation);
      ShowChild(row.root, "txtLabel", relation[0] != 0);
      SetObjectColor(Child(row.root, "txtLabel"),
                     index == LanMonitor::currentSystem ? "FFFFFF" : "FFA41C");
      LANProgress(row, index, computer);
    }
    // Graph coordinates can be negative. Normalize the complete graph once per
    // reconciliation without mutating the LAN model; links use these positions.
    if (minX < 0 || minY < 0) {
      for (auto &entry : systems)
        if (entry.second.generation == generation) {
          entry.second.root->x -= minX; entry.second.root->y -= minY;
        }
      maxX -= minX; maxY -= minY; minX = minY = 0;
    }
    for (int index = 0; index < computer->links.Size(); ++index) {
      if (!computer->links.ValidIndex(index)) continue;
      LanComputerLink *link = computer->links.GetData(index);
      if (!link || link->visible == LANLINKVISIBLE_NONE) continue;
      auto from = systems.find(link->from), to = systems.find(link->to);
      const bool fromKnown = from != systems.end() && from->second.generation == generation;
      const bool toKnown = to != systems.end() && to->second.generation == generation;
      if ((link->visible == LANLINKVISIBLE_FROMAWARE && !fromKnown) ||
          (link->visible == LANLINKVISIBLE_TOAWARE && !toKnown) ||
          (link->visible >= LANLINKVISIBLE_AWARE && (!fromKnown || !toKnown))) continue;
      Row &row = lanLinks[index];
      if (!row.root) {
        std::unique_ptr<UILine> line(new UILine());
        row.root = line.get(); line->name = "link:" + std::to_string(index);
        line->strokeWidth = 2; line->zOrder = -1;
        canvas->AddChild(std::move(line));
      }
      row.generation = generation;
      auto *line = static_cast<UILine *>(row.root);
      if (link->visible == LANLINKVISIBLE_FROMAWARE) {
        line->x1 = line->x2 = from->second.root->x + 24 + 42 * link->fromX;
        line->y1 = from->second.root->y + 65 + 25 * link->fromY - 2;
        line->y2 = line->y1 + 4;
      } else if (link->visible == LANLINKVISIBLE_TOAWARE) {
        line->x1 = line->x2 = to->second.root->x + 24 + 42 * link->toX;
        line->y1 = to->second.root->y + 65 + 25 * link->toY - 2;
        line->y2 = line->y1 + 4;
      } else {
        line->x1 = from->second.root->x + 24 + 42 * link->fromX;
        line->y1 = from->second.root->y + 65 + 25 * link->fromY;
        line->x2 = to->second.root->x + 24 + 42 * link->toX;
        line->y2 = to->second.root->y + 65 + 25 * link->toY;
      }
      const int a = LanMonitor::GetNodeIndex(link->from), b = LanMonitor::GetNodeIndex(link->to);
      const int admin = LanMonitor::GetNodeIndex(LanMonitor::sysAdminCurrentSystem);
      const bool connected = a >= 0 && b >= 0 && std::abs(a - b) == 1;
      line->strokeColor = connected && admin >= 0 && std::min(a, b) >= admin ? "D31A1A" :
                          connected ? "FFFFFF" : "1E62A8";
    }
    Retire(systems); Retire(lanLinks);
    for (auto it = scanTargets.begin(); it != scanTargets.end();) {
      if (!SvbGetTask(it->first)) it = scanTargets.erase(it);
      else ++it;
    }
    UIObject *network = lan.Find("network");
    canvas->w = maxX - minX; canvas->h = maxY - minY;
    canvas->minX = std::min(0.0f, network->w - canvas->w);
    canvas->maxX = canvas->w;
    canvas->minY = std::min(0.0f, network->h - canvas->h);
    canvas->maxY = canvas->h;
    canvas->x = std::max(canvas->minX, std::min(canvas->x, 0.0f));
    canvas->y = std::max(canvas->minY, std::min(canvas->y, 0.0f));
    const int selected = contextSystem >= 0 ? contextSystem : LanMonitor::currentSelected;
    LanComputerSystem *system = computer->systems.ValidIndex(selected) ? computer->systems.GetData(selected) : nullptr;
    if (system && system->visible >= LANSYSTEMVISIBLE_TYPE && system->TYPE >= 0 && system->TYPE < LANSYSTEM_NUMTYPES) {
      LanInterfaceObject *description = LanInterface::GetLanInterfaceObject(system->TYPE);
      SetText(lan.Find("sidePanel/txtTitle"), description->name);
      SetText(lan.Find("sidePanel/txtDesc"), description->description);
    } else {
      SetText(lan.Find("sidePanel/txtTitle"), system ? "Unknown system" : computer->name);
      SetText(lan.Find("sidePanel/txtDesc"), system ? "Probe this system to identify it." : "Select a system to view its details.");
    }
    const char *status = LanMonitor::sysAdminState == SYSADMIN_ASLEEP ? "Asleep" :
                         LanMonitor::sysAdminState == SYSADMIN_CURIOUS ? "Curious" :
                         LanMonitor::sysAdminState == SYSADMIN_SEARCHING ? "Searching" : "Found you";
    SetText(lan.Find("txtAdminStatus"), std::string("Admin Status: ") + status);
    SetObjectColor(lan.Find("txtAdminStatus"),
                   LanMonitor::sysAdminState < SYSADMIN_SEARCHING ? "2BAAFF" : "D31A1A");
    SetEnabled(lan.Find("controls/btnConnect"), LanMonitor::currentSelected != LanMonitor::currentSystem &&
               LanMonitor::currentSelected >= 0 && LanMonitor::IsInConnection(LanMonitor::currentSelected));
    SetEnabled(lan.Find("controls/btnReset"), true);
  }

  void UpdateBrowser() {
    const bool dialing = LegacyHasButton("dialler_number");
    const std::string host = player->remotehost;
    if (host != remoteIP) { remoteIP = host; browserStart = al_get_time(); }
    const bool transition = !remoteIP.empty() && al_get_time() - browserStart < 0.5;
    if (!dialing && !transition) { browser.Hide(); pendingIP.clear(); return; }
    if (!browser.Show()) return;
    browser.Root()->x = 958; browser.Root()->y = -1;
    RaiseWindow(browser);
    const std::string ip = dialing && !pendingIP.empty() ? pendingIP : host;
    VLocation *location = game->GetWorld()->GetVLocation(const_cast<char *>(ip.c_str()));
    SetText(browser.Find("connectTo/txtLocation"), location ? location->computer : ip);
    SetVisible(browser.Find("connectTo"), true);
    browser.Root()->alpha = dialing ? 1.0f : static_cast<float>(std::max(0.0, 1.0 - (al_get_time() - browserStart) / 0.5));
    if (map.IsVisible()) map.BringToFront();
    if (popup.IsVisible()) popup.Focus();
  }
  void OpenNotification() {
    player = CurrentPlayer();
    if (!player || !notificationObject) { notification.Hide(); return; }
    if (notificationIsMission) {
      for (int i = 0; i < player->missions.Size(); ++i)
        if (player->missions.GetData(i) == notificationObject) {
          game->GetInterface()->GetLocalInterface()->RunScreen(SCREEN_MISSION, i);
          break;
        }
    } else {
      for (int i = 0; i < player->messages.Size(); ++i)
        if (player->messages.ValidIndex(i) && player->messages.GetData(i) == notificationObject) {
          game->GetInterface()->GetLocalInterface()->RunScreen(SCREEN_EMAIL, i);
          break;
        }
    }
    notification.Hide(); notificationObject = nullptr;
  }
  void UpdateNotification() {
    int mails = 0, missions = 0;
    Message *mail = nullptr; Mission *mission = nullptr;
    for (int i = 0; i < player->messages.Size(); ++i) {
      if (!player->messages.ValidIndex(i)) continue;
      Message *message = player->messages.GetData(i);
      if (message && !message->IsRead()) { ++mails; mail = message; }
    }
    for (int i = 0; i < player->missions.Size(); ++i) {
      Mission *candidate = player->missions.GetData(i);
      if (candidate && !candidate->IsRead()) { ++missions; mission = candidate; }
    }
    UIObject *email = owner.FindChildRecursive("btnEmail");
    if (email) {
      UIObject *badge = Child(email, "notification");
      SetVisible(badge, mails + missions > 0);
      SetText(Child(badge, "txtNumber"), std::to_string(std::min(99, mails + missions)));
    }
    const bool newMission = missions > unreadMissions;
    if (mails > unreadMail || newMission) {
      notificationObject = newMission ? static_cast<UplinkObject *>(mission) : static_cast<UplinkObject *>(mail);
      notificationIsMission = newMission;
      if (!notification.Root()) notificationBound = false;
      if (notificationObject && notification.Show()) {
        notification.Root()->x = 12; notification.Root()->y = 906;
        RaiseWindow(notification);
        if (!notificationBound) {
          notification.Find("btnBase")->onClickCallback = [this](UIObject *) { OpenNotification(); };
          notificationBound = true;
        }
        SetText(notification.Find("txtSubject"), newMission ? mission->description : mail->GetSubject());
        FitText(notification.FindAs<UITextPoint>("txtSubject"), 205);
        SetText(notification.Find("txtContents"), newMission ? mission->GetFullDetails() : mail->GetBody());
        if (auto *body = notification.FindAs<UITextParagraph>("txtContents")) {
          body->clipChildren = true; body->PrepareText();
          if (body->lines.size() > 2) {
            body->text = body->text.substr(0, body->lines[1].end) + "...";
          }
        }
        Date &date = newMission ? mission->createdate : mail->date;
        char formatted[48];
        std::snprintf(formatted, sizeof(formatted), "%02d:%02d,\n%02d.%02d.%04d", date.GetHour(), date.GetMinute(), date.GetDay(), date.GetMonth(), date.GetYear());
        SetText(notification.Find("txtDate"), formatted);
        notificationStart = al_get_time();
        PlaySound("uplinkHD/sounds/email.wav");
      }
    }
    unreadMail = mails; unreadMissions = missions;
    if (notification.IsVisible()) {
      const double elapsed = al_get_time() - notificationStart;
      if (elapsed >= 5.5) notification.Hide();
      else notification.Root()->alpha = static_cast<float>(std::min(1.0, std::min(elapsed / 0.5, (5.5 - elapsed) / 0.5)));
    }
  }

  void Hide() {
    map.Hide(); lan.Hide(); browser.Hide(); notification.Hide(); menu.Hide(); popup.Hide();
  }
  void Update() {
    Player *current = CurrentPlayer();
    if (!current) { Hide(); player = sessionPlayer = nullptr; return; }
    if (current != sessionPlayer) {
      Hide();
      if (UIObject *container = map.Find("linksContainer", false)) {
        for (auto &row : locations) if (row.second.root) container->DetachChild(row.second.root);
        for (auto &row : routeLines) if (row.second.root) container->DetachChild(row.second.root);
      }
      locations.clear(); routeLines.clear();
      if (UIObject *canvas = lan.Find("network/elements", false)) canvas->ClearChildren();
      systems.clear(); lanLinks.clear(); scanTargets.clear(); lanComputer = nullptr;
      contextIP.clear(); pendingIP.clear(); contextSystem = -1;
      remoteIP = current->remotehost; browserStart = -1;
      unreadMail = unreadMissions = 0; notificationObject = nullptr;
      player = sessionPlayer = current;
    }
    if (WorldMapInterface::IsVisibleWorldMapInterface() == WORLDMAP_LARGE) {
      if (!map.IsVisible()) ShowMap();
    } else if (map.IsVisible()) map.Hide();
    if (game->GetInterface()->GetLocalInterface()->InScreen() == SCREEN_LAN) {
      if (!lan.IsVisible()) ShowLAN();
    } else if (lan.IsVisible()) lan.Hide();
    UpdateMap(); UpdateLAN(); UpdateBrowser(); UpdateNotification();
    if (menu.IsVisible()) menu.BringToFront();
    if (popup.IsVisible()) popup.Focus();
  }
  bool HandleInput(const ALLEGRO_EVENT &event) {
    if ((event.type == ALLEGRO_EVENT_KEY_CHAR || event.type == ALLEGRO_EVENT_KEY_UP) &&
        event.keyboard.keycode == swallowedKey) {
      if (event.type == ALLEGRO_EVENT_KEY_UP) swallowedKey = -1;
      return true;
    }
    if (popup.HandleInput(event)) return true;
    if (event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN) {
      UIObject *hit = owner.HitTest(event.mouse.x, event.mouse.y);
      for (HDLayoutView *view : {&map, &lan, &browser, &notification, &menu})
        if (view->IsVisible() && hit && hit->IsDescendantOf(view->Root())) {
          view->BringToFront();
          break;
        }
    }
    if (menu.IsVisible()) {
      if (event.type == ALLEGRO_EVENT_KEY_DOWN && event.keyboard.keycode == ALLEGRO_KEY_ESCAPE) {
        swallowedKey = event.keyboard.keycode;
        menu.Hide(); return true;
      }
      if (event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN && !menu.Root()->ContainsPoint(event.mouse.x, event.mouse.y)) {
        menu.Hide(); return true;
      }
      if (event.type == ALLEGRO_EVENT_KEY_DOWN) {
        const int key = event.keyboard.keycode;
        UIObject *entries = menu.Root()->FindChild("entries");
        if (entries && !entries->children.empty() &&
            (key == ALLEGRO_KEY_TAB || key == ALLEGRO_KEY_UP || key == ALLEGRO_KEY_DOWN)) {
          size_t index = 0;
          for (size_t i = 0; i < entries->children.size(); ++i)
            if (entries->children[i].get() == owner.GetFocus()) index = i;
          const bool previous = key == ALLEGRO_KEY_UP ||
              (key == ALLEGRO_KEY_TAB && (event.keyboard.modifiers & ALLEGRO_KEYMOD_SHIFT));
          index = previous ? (index + entries->children.size() - 1) % entries->children.size()
                           : (index + 1) % entries->children.size();
          owner.SetFocus(entries->children[index].get());
          swallowedKey = key;
          return true;
        }
        if (key == ALLEGRO_KEY_ENTER || key == ALLEGRO_KEY_PAD_ENTER || key == ALLEGRO_KEY_SPACE) {
          swallowedKey = key;
          UIObject *focused = owner.GetFocus();
          if (focused && focused->IsDescendantOf(menu.Root())) focused->OnClick();
          return true;
        }
      }
      // Letter shortcuts stay with the configurable command dispatcher. Its
      // Invoke call dismisses this menu before performing the live action.
    }
    if (event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN && event.mouse.button == 2) {
      UIObject *hit = owner.HitTest(event.mouse.x, event.mouse.y);
      if (map.IsVisible()) {
        for (const auto &row : locations) {
          UIObject *button = Child(row.second.root, "btnLink");
          if (button && hit && hit->IsDescendantOf(button)) {
            contextIP = row.first;
            OpenMenu(event.mouse.x, event.mouse.y, false);
            return true;
          }
        }
      } else if (lan.IsVisible()) {
        for (const auto &row : systems) {
          if (hit && hit->IsDescendantOf(row.second.root)) {
            contextSystem = row.first;
            OpenMenu(event.mouse.x, event.mouse.y, true);
            return true;
          }
        }
      }
    }
    if (event.type == ALLEGRO_EVENT_KEY_DOWN && event.keyboard.keycode == ALLEGRO_KEY_ESCAPE && map.IsVisible()) {
      swallowedKey = event.keyboard.keycode;
      Invoke("Close World Map"); return true;
    }
    return false;
  }
};

HDMapLan::HDMapLan(UIObject &root) : state(new State(root)) {}
HDMapLan::~HDMapLan() noexcept = default;
void HDMapLan::Update() { state->Update(); }
void HDMapLan::Hide() { state->Hide(); }
bool HDMapLan::IsVisible() const {
  return state->map.IsVisible() || state->lan.IsVisible() || state->browser.IsVisible() ||
         state->notification.IsVisible() || state->menu.IsVisible() || state->popup.IsVisible();
}
bool HDMapLan::ShowMap() { return state->ShowMap(); }
bool HDMapLan::ShowLAN() { return state->ShowLAN(); }
bool HDMapLan::IsMapVisible() const { return state->map.IsVisible(); }
bool HDMapLan::IsLANVisible() const { return state->lan.IsVisible(); }
bool HDMapLan::CapturesKeyboard() const {
  return state->map.IsVisible() || state->lan.IsVisible() ||
         state->menu.IsVisible() || state->popup.IsVisible();
}
void HDMapLan::SetContextLocation(const std::string &ip) { state->contextIP = ip; }
bool HDMapLan::Invoke(const std::string &command) { return state->Invoke(command); }
bool HDMapLan::Invoke(const std::string &command, const std::string &ip) {
  state->contextIP = ip;
  return state->Invoke(command);
}
bool HDMapLan::HandleInput(const ALLEGRO_EVENT &event) { return state->HandleInput(event); }

} // namespace HDUI
