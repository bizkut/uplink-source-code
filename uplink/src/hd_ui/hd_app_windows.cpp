#include "hd_app_windows.h"
#include "hd_legacy_bridge.h"
#include "hd_hotkeys.h"
#include "hd_screens.h"
#include "app/app.h"
#include "app/globals.h"
#include "app/serialise.h"
#include "world/player.h"
#include "game/game.h"
#include "interface/interface.h"
#include "interface/remoteinterface/remoteinterface.h"
#include "interface/remoteinterface/remoteinterfacescreen.h"
#include "interface/taskmanager/taskmanager.h"
#include "interface/taskmanager/passwordbreaker.h"
#include "interface/taskmanager/dictionaryhacker.h"
#include "interface/taskmanager/decypher.h"
#include "interface/taskmanager/firewalldisable.h"
#include "interface/taskmanager/proxydisable.h"
#include "interface/taskmanager/securitybypass.h"
#include "interface/taskmanager/logmodifier.h"
#include "interface/taskmanager/voiceanalyser.h"
#include "interface/taskmanager/gatewaynuke.h"
#include "interface/taskmanager/uplinkagentlist.h"
#include "interface/taskmanager/filecopier.h"
#include "interface/taskmanager/filedeleter.h"
#include "interface/taskmanager/decrypter.h"
#include "interface/taskmanager/logdeleter.h"
#include "interface/taskmanager/logundeleter.h"
#include "interface/taskmanager/iplookup.h"
#include "interface/taskmanager/ipprobe.h"
#include "mainmenu/mainmenu.h"
#include "world/world.h"
#include "world/computer/computer.h"
#include "world/computer/recordbank.h"
#include "world/computer/securitysystem.h"
#include "world/computer/computerscreen/passwordscreen.h"
#include "world/computer/computerscreen/useridscreen.h"
#include "world/computer/computerscreen/cypherscreen.h"
#include "world/generator/numbergenerator.h"
#include "vanbakel.h"
#include "soundgarden.h"
#include "redshirt.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
#include <utility>

namespace HDUI {
namespace {
struct AppDefinition {
  const char *name, *layout, *icon;
};
const AppDefinition definitions[] = {
  {"Password_Breaker", "AppPasswordBreaker", "passwordBreaker"},
  {"Dictionary_Hacker", "AppDictionaryHacker", "dictionaryHacker"},
  {"Decypher", "AppDecypher", "decypher"},
  {"Firewall_Disable", "AppFirewallDisable", "firewallDisable"},
  {"Proxy_Disable", "AppProxyDisable", "proxyDisable"},
  {"Firewall_Bypass", "AppFirewallBypass", "firewallBypass"},
  {"Proxy_Bypass", "AppProxyBypass", "proxyBypass"},
  {"Monitor_Bypass", "AppMonitorBypass", "monitorBypass"},
  {"IP_Lookup", "AppIPLookUp", "ipLookup"},
  {"IP_Probe", "AppIPProbe", "ipProbe"},
  {"Log_Modifier", "AppLogModifier", "logModifier"},
  {"Voice_Analyser", "AppVoiceAnalyser", "voiceAnalyser"},
  {"Trace_Tracker", "AppTraceTracker", "traceTracker"},
  {"Revelation_Tracker", "AppRevelationTracker", "revelationTracker"},
  {"Motion_Sensor", "AppMotionSensor", "motionSensor"},
  {"Gateway_Nuke", "AppGatewayNuke", "gatewayNuke"},
  {"Uplink_Agent_List", "AppUplinkAgentList", "uplinkAgentList"},
  {"File_Copier", nullptr, "unknown"}, {"File_Deleter", nullptr, "unknown"},
  {"Log_Deleter", nullptr, "logDeleter"}, {"Log_UnDeleter", nullptr, "logUnDeleter"},
  {"Decrypter", nullptr, "decrypter"}, {"Defrag", nullptr, "unknown"},
  {"Encryption_Bypass", nullptr, "unknown"}, {"Revelation", nullptr, "revelation"},
  {"Faith", nullptr, "faith"}, {"Tutorial", nullptr, "unknown"},
  {"LAN_Scan", nullptr, "lanScan"}, {"LAN_Probe", nullptr, "lanProbe"},
  {"LAN_Spoof", nullptr, "lanSpoof"}, {"LAN_Force", nullptr, "lanForce"},
  {"HUD_ConnectionAnalysis", nullptr, "hudConnectionAnalysis"},
  {"HUD_IRC-Client", nullptr, "hudIRCClient"},
  {"HUD_LANView", nullptr, "hudLANView"}
};
const AppDefinition *Definition(const std::string &name) {
  for (const auto &definition : definitions)
    if (name == definition.name) return &definition;
  return nullptr;
}
struct HUDApplication {
  const char *name, *button;
  char upgrade;
};
const HUDApplication hudApplications[] = {
  {"HUD_ConnectionAnalysis", "hud_analyser", HUDUPGRADE_CONNECTIONANALYSIS},
  {"HUD_IRC-Client", "hud_ircclient", HUDUPGRADE_IRCCLIENT},
  {"HUD_LANView", "hud_lanview", HUDUPGRADE_LANVIEW}
};
const HUDApplication *HUDDefinition(const std::string &name) {
  for (const auto &application : hudApplications)
    if (name == application.name) return &application;
  return nullptr;
}
bool InGame() {
  // A paused session is live, but a newly constructed Game has no world yet.
  return app && !app->Closed() && game && game->GameSpeed() >= GAMESPEED_PAUSED &&
      !app->GetMainMenu()->IsVisible() && LegacyHasButton("hud_mainmenu");
}
Player *CurrentPlayer() { return InGame() ? game->GetWorld()->GetPlayer() : nullptr; }
Computer *RemoteComputer() {
  Player *player = CurrentPlayer();
  VLocation *host = player ? player->GetRemoteHost() : nullptr;
  return host ? host->GetComputer() : nullptr;
}
ComputerScreen *CurrentScreen() {
  if (!InGame()) return nullptr;
  RemoteInterface *remote = game->GetInterface()->GetRemoteInterface();
  return remote && remote->IsVisible() ? remote->GetComputerScreen() : nullptr;
}
std::string ButtonName(const char *prefix, int pid) {
  return std::string(prefix) + " " + std::to_string(pid);
}
std::string Caption(const char *prefix, int pid) {
  const std::string name = ButtonName(prefix, pid);
  return LegacyHasButton(name.c_str()) ? LegacyCaption(name.c_str()) : std::string();
}
bool Click(const char *prefix, int pid) {
  const std::string name = ButtonName(prefix, pid);
  return LegacyHasButton(name.c_str()) && LegacyClick(name.c_str());
}
float Fraction(double progress, double total) {
  return total > 0 ? static_cast<float>(std::max(0.0, std::min(1.0, progress / total))) : 0;
}
template <class T> void Progress(const T &task, HDAppTaskSnapshot &result) {
  result.status = task.GetStatus();
  result.progress = Fraction(task.GetProgress(), task.GetTicksRequired());
}
std::string DisplayName(std::string name, float version) {
  std::replace(name.begin(), name.end(), '_', ' ');
  char suffix[32];
  std::snprintf(suffix, sizeof(suffix), " v%.1f", version);
  return name + suffix;
}
float OwnedVersion(const std::string &name) {
  Player *player = CurrentPlayer();
  if (!player) return 0;
  if (const HUDApplication *hud = HUDDefinition(name))
    return player->gateway.HasHUDUpgrade(hud->upgrade) ? 1.0f : 0.0f;
  DataBank &bank = player->gateway.databank;
  float version = 0;
  for (int i = 0; i < bank.GetDataSize(); ++i) {
    Data *file = bank.GetDataFile(i);
    if (file && file->TYPE == DATATYPE_PROGRAM && file->encrypted == 0 &&
        file->compressed == 0 && name == file->title)
      version = std::max(version, file->version);
  }
  return version;
}
bool ValidContext(const std::string &name) {
  if (!InGame()) return false;
  if (const HUDApplication *hud = HUDDefinition(name)) {
    if (!LegacyHasButton(hud->button)) return false;
    if (name == "HUD_LANView")
      return CurrentPlayer()->IsConnected() && RemoteComputer() && RemoteComputer()->TYPE == COMPUTER_TYPE_LAN;
    return true;
  }
  if (name == "Password_Breaker" || name == "Dictionary_Hacker")
    return (dynamic_cast<PasswordScreen *>(CurrentScreen()) && LegacyHasButton("passwordscreen_password")) ||
           (dynamic_cast<UserIDScreen *>(CurrentScreen()) && LegacyHasButton("useridscreen_code"));
  if (name == "Decypher")
    return dynamic_cast<CypherScreen *>(CurrentScreen()) && LegacyHasButton("cypherscreen_cypher");
  if (name == "Firewall_Disable" || name == "Proxy_Disable" || name.find("_Bypass") != std::string::npos) {
    Computer *computer = RemoteComputer();
    if (!CurrentPlayer()->IsConnected() || !computer) return false;
    const int type = name.find("Firewall") == 0 ? SECURITY_TYPE_FIREWALL :
        name.find("Proxy") == 0 ? SECURITY_TYPE_PROXY :
        name.find("Monitor") == 0 ? SECURITY_TYPE_MONITOR : SECURITY_TYPE_ENCRYPTION;
    for (int i = 0; i < computer->security.NumSystems(); ++i) {
      SecuritySystem *system = computer->security.GetSystem(i);
      if (!system || system->TYPE != type) continue;
      if (name.find("_Bypass") == std::string::npos) return true;
      return LegacyHasButton(("analyser_system " + std::to_string(i)).c_str());
    }
    return false;
  }
  // The LAN controller validates the selected system and concurrency rules.
  if (name.compare(0, 4, "LAN_") == 0) return false;
  // File/log context commands are handled by their selected-row controllers;
  // launching from the software menu deliberately enters real target mode.
  if (name == "File_Copier" || name == "File_Deleter" || name == "Decrypter" ||
      name == "Log_Deleter" || name == "Log_UnDeleter" || name == "Log_Modifier")
    return false;
  if (name == "Voice_Analyser") {
    RemoteInterface *remote = game->GetInterface()->GetRemoteInterface();
    if (!remote || !remote->IsVisible()) return false;
    const int screen = remote->GetInterfaceScreen()->ScreenID();
    return screen == SCREEN_VOICEPHONE || screen == SCREEN_VOICEANALYSIS;
  }
  return true;
}
Record *UserRecord(Computer *computer) {
  if (!computer) return nullptr;
  std::string name = LegacyHasButton("useridscreen_name") ? LegacyCaption("useridscreen_name") : "admin";
  if (name.empty()) name = "admin";
  return computer->recordbank.GetRecordFromName(const_cast<char *>(name.c_str()));
}
unsigned Cost(const std::string &name, float version) {
  Computer *computer = RemoteComputer();
  ComputerScreen *screen = CurrentScreen();
  if (name == "Log_Deleter") return 240;
  if (name == "Log_UnDeleter") return 180;
  if (name == "Password_Breaker") {
    int difficulty = 1;
    if (auto *password = dynamic_cast<PasswordScreen *>(screen)) difficulty = password->difficulty;
    if (auto *userid = dynamic_cast<UserIDScreen *>(screen)) {
      Record *record = UserRecord(computer);
      char field[] = "Security";
      const char *value = record ? record->GetField(field) : nullptr;
      int security = 0;
      static const float modifiers[] = {0, 2, 1.5f, 1.2f, 1, 1, .9f, .7f, .5f, .5f, .5f};
      if (value && std::sscanf(value, "%d", &security) == 1 && security >= 0 && security <= 10)
        difficulty = static_cast<int>(userid->difficulty * modifiers[security]);
    }
    return difficulty >= 1 ? 6u * NumberGenerator::ApplyVariance(difficulty, 15) : 20u;
  }
  if (name == "Dictionary_Hacker") {
    const char *password = nullptr;
    if (auto *target = dynamic_cast<PasswordScreen *>(screen)) password = target->password;
    if (dynamic_cast<UserIDScreen *>(screen)) {
      Record *record = UserRecord(computer);
      char field[] = "Password";
      password = record ? record->GetField(field) : nullptr;
    }
    World *world = game->GetWorld();
    for (int i = 0; password && i < world->passwords.Size(); ++i)
      if (world->passwords.ValidIndex(i) && !std::strcmp(world->passwords.GetData(i), password))
        return static_cast<unsigned>((i == 0 ? 2 : i) * .2f);
    return 0;
  }
  if (name == "Decypher") {
    auto *cypher = dynamic_cast<CypherScreen *>(screen);
    const int difficulty = cypher ? 42 * cypher->difficulty : 1;
    return version >= 1 ? NumberGenerator::ApplyVariance(difficulty, 15) / static_cast<int>(version) : 0;
  }
  if (name == "Firewall_Disable" || name == "Proxy_Disable")
    return (name == "Firewall_Disable" ? 80u : 100u) * (computer ? computer->security.NumSystems() : 0);
  if (name == "LAN_Force") return 100;
  if (name == "LAN_Probe") return 170;
  if (name == "LAN_Scan") return 300;
  if (name == "LAN_Spoof") return 70;
  if (name == "Faith") return 5;
  if (name == "Revelation") return 200;
  if (name == "Log_Modifier") return 100;
  if (name == "File Delete Remote" || name == "File Delete Local") return static_cast<unsigned>(version * 30);
  if (name == "Decrypt File Local") return static_cast<unsigned>(version * 270);
  if (name == "File Download" || name == "File Upload" || name == "Decrypt File Remote") {
    unsigned count = 0;
    for (int i = 0; i < SvbNumTasks(); ++i) {
      TaskWrapper *wrapper = SvbGetTaskWrapperAtIndex(i);
      if (wrapper && wrapper->name && name == wrapper->name) ++count;
    }
    const int bandwidth = CurrentPlayer()->gateway.GetBandwidth();
    return bandwidth > 0 && count ? static_cast<unsigned>(version * (name == "Decrypt File Remote" ? 270 : 135) * count / bandwidth) : 0;
  }
  return 0;
}
void SetImage(UIObject *object, const std::string &file) {
  auto *image = dynamic_cast<UIImage *>(object);
  if (!image || image->file == file) return;
  image->file = file;
  std::string base = app ? app->path : "";
  if (!base.empty() && base.back() != '/') base += '/';
  image->LoadBitmap(base + "uplinkHD/graphics");
}
void Metadata(UIObject *root, const char *name, const std::string &value) {
  if (!root) return;
  UIObject *field = root->FindChild(name);
  if (!field) {
    std::unique_ptr<UITextPoint> text(new UITextPoint);
    text->name = name;
    text->visible = false;
    field = text.get();
    root->AddChild(std::move(text));
  }
  SetText(field, value);
}
int LaunchAppTask(const std::string &name, float version) {
  int previousPID = 0;
  for (int i = 0; i < SvbNumTasks(); ++i) {
    TaskWrapper *wrapper = SvbGetTaskWrapperAtIndex(i);
    if (wrapper) previousPID = std::max(previousPID, wrapper->pid);
  }
  // RunSoftware owns task creation and enforces the shared instance limit.
  game->GetInterface()->GetTaskManager()->RunSoftware(const_cast<char *>(name.c_str()), version);
  for (int i = 0; i < SvbNumTasks(); ++i) {
    TaskWrapper *wrapper = SvbGetTaskWrapperAtIndex(i);
    if (wrapper && wrapper->pid > previousPID && wrapper->name && name == wrapper->name)
      return wrapper->pid;
  }
  return -1;
}
} // namespace

int AppInstanceLimit(const char *name) {
  // GOLD HD_App::Create (004ADD70): five by default, one for these names.
  return name && (!std::strcmp(name, "Log_Modifier") || !std::strcmp(name, "Log_Scan") ||
                  !std::strcmp(name, "Log_Spoof") || !std::strcmp(name, "Revelation") ||
                  !std::strcmp(name, "Faith")) ? 1 : 5;
}
void RefreshAppScheduling(int pid) {
  TaskWrapper *wrapper = SvbGetTaskWrapper(pid);
  auto *task = wrapper ? dynamic_cast<UplinkTask *>(wrapper->task) : nullptr;
  Player *player = CurrentPlayer();
  if (!task || !player) return;
  // GOLD 004AF940/004ADD70/004AE590: cost and threshold are sampled on
  // creation/start, never re-randomized by redraw or a hidden app window.
  unsigned cost = 0;
  if (auto *copy = dynamic_cast<FileCopier *>(task)) {
    if (copy->GetSource()) {
      int count = 0;
      for (int i = 0; i < SvbNumTasks(); ++i) {
        TaskWrapper *peer = SvbGetTaskWrapperAtIndex(i);
        auto *other = peer ? dynamic_cast<FileCopier *>(peer->task) : nullptr;
        if (other && other->GetSource() && other->IsRemoteFile() == copy->IsRemoteFile()) ++count;
      }
      const int bandwidth = player->gateway.GetBandwidth();
      if (bandwidth > 0) cost = static_cast<unsigned>(copy->GetSourceSize() * 135.0 * count / bandwidth);
    }
  } else if (auto *remove = dynamic_cast<FileDeleter *>(task)) {
    cost = static_cast<unsigned>(remove->GetSourceSize() * 30);
  } else if (auto *decrypt = dynamic_cast<Decrypter *>(task)) {
    int count = 0;
    for (int i = 0; i < SvbNumTasks(); ++i) {
      TaskWrapper *peer = SvbGetTaskWrapperAtIndex(i);
      auto *other = peer ? dynamic_cast<Decrypter *>(peer->task) : nullptr;
      if (other && other->GetSource() && other->IsRemoteFile()) ++count;
    }
    const int bandwidth = player->gateway.GetBandwidth();
    cost = decrypt->IsRemoteFile() ? (bandwidth > 0 ?
        static_cast<unsigned>(decrypt->GetSourceSize() * 270.0 * count / bandwidth) : 0u) :
        static_cast<unsigned>(decrypt->GetSourceSize() * 270);
  } else cost = Cost(wrapper->name ? wrapper->name : "", task->version);
  const int speed = player->gateway.GetCPUSpeed();
  double minimum = speed > 0 ? (cost ? cost : 240.0) / speed / 10.0 : 1.0;
  if (minimum > 1) minimum *= .5;
  SvbSetMinimumCPUShare(pid, minimum);
}

int LaunchOwnedApp(const std::string &name) {
  const float version = OwnedVersion(name);
  if (!InGame() || !Definition(name) || HUDDefinition(name) || version <= 0) return -1;
  return LaunchAppTask(name, version);
}

int LaunchBuiltinApp(const std::string &name) {
  if (!InGame() || (name != "File_Copier" && name != "File_Deleter")) return -1;
  return LaunchAppTask(name, 1.0f);
}

bool ReadAppTask(int pid, HDAppTaskSnapshot &result) {
  TaskWrapper *wrapper = SvbGetTaskWrapper(pid);
  auto *task = wrapper ? dynamic_cast<UplinkTask *>(wrapper->task) : nullptr;
  if (!task || !InGame()) return false;
  result.pid = pid;
  result.name = wrapper->name ? wrapper->name : "";
  result.version = task->version;
  result.priority = wrapper->priority;
  result.target = task->GetTargetString() ? task->GetTargetString() : "";
  result.status = 0;
  result.progress = 0;
  result.running = result.complete = result.failed = false;
  result.statusText = "Select target";
  result.detail.clear();
  if (auto *app = dynamic_cast<PasswordBreaker *>(task)) {
    result.running = app->GetLength() > 0;
    result.complete = app->HasCompleted();
    result.progress = app->GetLength() > 0 ?
      Fraction(app->GetCurrentCharacter() + Fraction(app->GetProgress(), app->GetTicksRequired()), app->GetLength()) : 0;
    result.failed = result.running && app->GetTicksRequired() < 0;
    result.detail = app->GetCaption();
  } else if (auto *app = dynamic_cast<DictionaryHacker *>(task)) {
    result.status = app->GetStatus();
    result.running = result.status == DICTIONARYHACKER_INPROGRESS;
    result.progress = Fraction(app->GetCurrentIndex(), game->GetWorld()->passwords.Size());
    const char *word = game->GetWorld()->GetPassword(app->GetCurrentIndex());
    result.detail = result.running && word ? word : "";
    result.complete = app->HasCompleted();
    result.failed = app->HasFailed();
  } else if (auto *app = dynamic_cast<Decypher *>(task)) {
    Progress(*app, result);
    result.running = result.status == DECYPHER_INPROGRESS;
    result.complete = result.status == DECYPHER_FINISHED;
    result.detail = Caption("decypher_progress", pid);
  } else if (auto *app = dynamic_cast<FirewallDisable *>(task)) {
    Progress(*app, result);
    result.target = app->GetTargetIP();
    result.running = result.status == 1 || result.status == 2;
    result.failed = result.status == 3;
    result.complete = result.status == 4;
    result.progress = result.status == 1 ? result.progress / 3 : result.status == 2 ? (1 + 2 * result.progress) / 3 : result.complete ? 1 : 0;
    result.detail = Caption("firewalldisable_progress", pid);
  } else if (auto *app = dynamic_cast<ProxyDisable *>(task)) {
    Progress(*app, result);
    result.target = app->GetTargetIP();
    result.running = result.status == 1 || result.status == 2;
    result.failed = result.status == 3;
    result.complete = result.status == 4;
    result.progress = result.status == 1 ? result.progress / 3 : result.status == 2 ? (1 + 2 * result.progress) / 3 : result.complete ? 1 : 0;
    result.detail = Caption("proxydisable_progress", pid);
  } else if (auto *app = dynamic_cast<SecurityBypass *>(task)) {
    result.status = app->status;
    result.target = app->GetTargetObject() ? app->GetTargetIP() : "";
    result.running = app->status == 1;
    result.failed = app->status == 0 && app->GetTargetObject();
    if (app->status == 2) result.statusText = "Stopped";
  } else if (auto *app = dynamic_cast<LogModifier *>(task)) {
    Progress(*app, result);
    result.target = std::to_string(app->GetSourceIndex());
    result.running = result.status == LOGMODIFIER_STATUS_MODIFYING || result.status == LOGMODIFIER_STATUS_CREATING;
    result.complete = result.status == LOGMODIFIER_STATUS_FINISHED;
    result.detail = Caption("logmodifier_ltype", pid);
  } else if (auto *app = dynamic_cast<VoiceAnalyser *>(task)) {
    Progress(*app, result);
    result.target = app->GetPersonIP();
    result.detail = app->GetPersonName();
    result.running = result.status == VOICEANALYSER_STATUS_RECORDING || result.status == VOICEANALYSER_STATUS_ANALYSING ||
                     result.status == VOICEANALYSER_STATUS_REPRODUCING || result.status == VOICEANALYSER_STATUS_INPROGRESS;
    result.complete = result.status == VOICEANALYSER_STATUS_READY;
    result.statusText = Caption("analyser_text", pid);
  } else if (auto *app = dynamic_cast<GatewayNuke *>(task)) {
    result.status = app->GetStatus();
    result.failed = result.status == GATEWAYNUKE_STATUS_FAILED;
    result.statusText = Caption("gatenuke_title", pid);
  } else if (auto *app = dynamic_cast<IPLookup *>(task)) {
    result.status = app->GetStatus();
    result.running = result.status == IPLOOKUP_INPROGRESS;
    result.complete = result.status == IPLOOKUP_FINISHED;
    result.detail = Caption("iplookup_display", pid);
    result.failed = result.detail == "IP Not found";
  } else if (auto *app = dynamic_cast<IPProbe *>(task)) {
    result.status = app->GetStatus();
    result.running = result.status == PROBE_INPROGRESS;
    result.complete = result.status == PROBE_FINISHED;
    result.detail = Caption("probe_display", pid);
    result.failed = result.detail == "IP Not found";
  } else if (auto *app = dynamic_cast<FileCopier *>(task)) {
    Progress(*app, result);
    result.target = app->GetSourceTitle();
    result.running = result.status == FILECOPIER_INPROGRESS;
    result.complete = result.status == FILECOPIER_WAITINGFORTARGET || result.status == FILECOPIER_FINISHED;
  } else if (auto *app = dynamic_cast<FileDeleter *>(task)) {
    Progress(*app, result);
    result.target = app->GetSourceTitle();
    result.running = result.status == FILEDELETER_INPROGRESS;
    result.complete = result.status == FILEDELETER_FINISHED;
  } else if (auto *app = dynamic_cast<Decrypter *>(task)) {
    Progress(*app, result);
    result.target = app->GetSourceTitle();
    result.running = result.status == DECRYPTER_INPROGRESS;
    result.complete = result.status == DECRYPTER_FINISHED;
  } else if (auto *app = dynamic_cast<LogDeleter *>(task)) {
    Progress(*app, result);
    result.target = std::to_string(app->GetSourceIndex());
    result.running = result.status == LOGDELETER_INPROGRESS || result.status == LOGDELETER_REPLACING;
    result.complete = result.status == LOGDELETER_FINISHED;
  } else if (auto *app = dynamic_cast<LogUnDeleter *>(task)) {
    Progress(*app, result);
    result.target = std::to_string(app->GetSourceIndex());
    result.running = result.status == LOGUNDELETER_INPROGRESS;
    result.complete = result.status == LOGUNDELETER_FINISHED;
  } else if (result.name == "Trace_Tracker") {
    result.statusText = Caption("tracetracker_display", pid);
    result.running = CurrentPlayer()->GetConnection()->TraceInProgress();
    result.failed = CurrentPlayer()->GetConnection()->Traced();
  } else if (result.name == "Motion_Sensor") {
    result.status = CurrentPlayer()->gateway.proximity;
    result.detail = std::to_string(result.status) + " Persons";
    result.statusText = result.detail;
  }
  if (result.failed && result.statusText == "Select target") result.statusText = "Failed";
  else if (result.complete && result.statusText == "Select target") result.statusText = "Complete";
  else if (result.running && result.statusText == "Select target") result.statusText = "Running";
  return true;
}

class HDAppManager::Impl {
public:
  explicit Impl(UIObject &root) : owner(root), menu(root, "HUD/AppMenu"),
      popup(root), rowFactory("HUD/AppElement"), titleFactory("HUD/AppMenu", "txtCategTitleRef"),
      lineFactory("HUD/AppMenu", "categTitleSepRef"), menuWanted(false), windowsWanted(true), hotkeys(nullptr) {
    SvbSetTaskRemovalObserver([](int pid, void *owner) {
      static_cast<Impl *>(owner)->BeforeRemove(pid);
    }, this);
  }
  ~Impl() { SvbClearTaskRemovalObserver(this); }
  struct Window {
    explicit Window(UIObject &root, const char *layout)
        : view(root, std::string("HUD/apps/") + layout), error(root, "HUD/apps/AppError") {}
    HDLayoutView view, error;
    HDAppTaskSnapshot state;
    bool wired = false, seen = false;
    int editingSource = -1;
    bool terminal = false, closing = false;
    double expires = 0;
    std::vector<std::string> legacyNames;
  };
  struct Software {
    float version = 0;
    int category = 0;
    bool seen = false;
    std::string hotkey;
  };
  UIObject &owner;
  HDLayoutView menu;
  HDPopupController popup;
  HDRepeatLayoutFactory rowFactory, titleFactory, lineFactory;
  std::map<int, std::unique_ptr<Window>> windows;
  std::map<std::string, Software> software;
  bool menuWanted, windowsWanted;
  const HDHotkeyManager *hotkeys;

  void BeforeRemove(int pid) {
    HDAppTaskSnapshot state;
    if (!ReadAppTask(pid, state) || (!state.complete && !state.failed)) return;
    const AppDefinition *definition = Definition(state.name);
    if (!definition || !definition->layout) return;
    auto found = windows.find(pid);
    if (found == windows.end())
      found = windows.emplace(pid, std::unique_ptr<Window>(new Window(owner, definition->layout))).first;
    Window &window = *found->second;
    if (window.closing) return;
    window.state = std::move(state);
    window.terminal = true;
    window.expires = EclGetAccurateTime() + 2000;
  }

  void Hide() {
    menuWanted = windowsWanted = false;
    menu.Hide();
    popup.Hide();
    for (auto &item : windows) { item.second->view.Hide(); item.second->error.Hide(); }
  }
  bool Visible() const {
    if (menu.IsVisible() || popup.IsVisible()) return true;
    for (const auto &item : windows) if (item.second->view.IsVisible()) return true;
    return false;
  }
  bool ShowMenu() {
    if (!InGame()) return false;
    menuWanted = windowsWanted = true;
    menu.SetCaptureInput(true);
    if (!menu.Show()) return false;
    UIObject *root = menu.Root();
    root->y = 60;
    root->interactive = true;
    if (root->parent)
      root->parent->onClickCallback = [this](UIObject *) { menuWanted = false; menu.Hide(); };
    root->onClickCallback = [this](UIObject *) { menu.BringToFront(); };
    SetVisible(menu.Find("txtCategTitleRef"), false);
    SetVisible(menu.Find("categTitleSepRef"), false);
    UpdateSoftware();
    return true;
  }
  void Close(int pid) {
    if (!InGame()) return;
    auto found = windows.find(pid);
    if (found != windows.end()) {
      found->second->closing = true;
      found->second->terminal = false;
    }
    Task *task = SvbGetTask(pid);
    if (!task) return;
    // A bypass owns a live security mutation; undo it before deleting its PID,
    // exactly as SecurityBypass::CloseClick does (its destructor does not).
    if (auto *bypass = dynamic_cast<SecurityBypass *>(task)) bypass->EndCurrentBypass();
    if (game->GetInterface()->GetTaskManager()->GetTargetProgram() == task)
      game->GetInterface()->GetTaskManager()->SetTargetProgram(-1);
    SvbRemoveTask(pid);
    // Trees are reconciled next Update, never freed on their own callback stack.
  }
  bool SelectTarget(int pid) {
    if (!InGame() || !SvbGetTask(pid)) return false;
    auto *task = dynamic_cast<UplinkTask *>(SvbGetTask(pid));
    if (task) task->SetFollowMouse(false);
    game->GetInterface()->GetTaskManager()->SetTargetProgram(pid);
    return true;
  }
  bool Start(int pid) {
    if (!InGame()) return false;
    HDAppTaskSnapshot state;
    if (!ReadAppTask(pid, state)) return false;
    if (auto *bypass = dynamic_cast<SecurityBypass *>(SvbGetTask(pid))) {
      if (bypass->status == 1) { bypass->PauseCurrentBypass(); return true; }
      if (bypass->status == 2) { bypass->ResumeCurrentBypass(); return true; }
      SelectTarget(pid);
      Computer *computer = RemoteComputer();
      if (!computer || !CurrentPlayer()->IsConnected()) return true;
      for (int i = 0; i < computer->security.NumSystems(); ++i) {
        SecuritySystem *system = computer->security.GetSystem(i);
        if (!system || system->TYPE != bypass->TYPE) continue;
        // SetTarget needs the real analyser button for its legacy placement.
        const std::string target = "analyser_system " + std::to_string(i);
        if (LegacyHasButton(target.c_str()))
          game->GetInterface()->GetTaskManager()->SetProgramTarget(&computer->security, const_cast<char *>(target.c_str()), i);
        return true;
      }
      return true;
    }
    if (state.name == "Gateway_Nuke") {
      popup.ShowConfirm("Gateway Nuke", "Destroy your gateway and all evidence?", [this, pid]() {
        if (!SvbGetTask(pid)) return;
        Click("gatenuke_yes", pid);
        auto *nuke = dynamic_cast<GatewayNuke *>(SvbGetTask(pid));
        if (nuke && nuke->GetStatus() == GATEWAYNUKE_STATUS_AREYOUSURE) Click("gatenuke_yes", pid);
      });
      return true;
    }
    if (state.name == "Firewall_Disable" || state.name == "Proxy_Disable") {
      if (state.running) { Close(pid); return true; }
      if (!ValidContext(state.name)) return false;
      const bool clicked = Click(state.name == "Firewall_Disable" ? "firewalldisable_title" : "proxydisable_title", pid);
      if (clicked) RefreshAppScheduling(pid);
      return clicked;
    }
    if (state.name == "Voice_Analyser") {
      if (state.complete) return Click("analyser_play", pid);
      return SelectTarget(pid);
    }
    if (state.running) { Close(pid); return true; }
    if (!SelectTarget(pid)) return false;
    if (state.name == "Password_Breaker" || state.name == "Dictionary_Hacker") {
      if (!ValidContext(state.name)) return true;
      return LegacyClickFirst({"useridscreen_code", "passwordscreen_password"});
    }
    if (state.name == "Decypher")
      return !ValidContext(state.name) || LegacyClick("cypherscreen_cypher");
    return true;
  }
  void Bind(Window &window, int pid) {
    if (!window.view.Load()) return;
    UIObject *root = window.view.Root();
    root->interactive = true;
    root->x = 500 + (pid % 8) * 105;
    root->y = 130 + ((pid / 8) % 5) * 105;
    root->onClickCallback = [this, pid](UIObject *) {
      auto found = windows.find(pid);
      if (found != windows.end()) found->second->view.BringToFront();
      if (found != windows.end() && found->second->state.name == "Log_Modifier" &&
          !found->second->state.running) SelectTarget(pid);
      if (found != windows.end() && found->second->state.name == "Trace_Tracker" && SvbGetTask(pid))
        Click("tracetracker_audio", pid);
    };
    const float titleY = window.state.name == "Log_Modifier" ? 14 : 0;
    const float titleHeight = root->h > 100 ? 28 : 4;
    UIObject *closeButton = window.view.Find("btnClose", false);
    std::unique_ptr<UIObject> drag = MakeDraggableTitle(0, titleY, closeButton ? closeButton->x : root->w, titleHeight);
    if (window.state.name == "Log_Modifier")
      drag->onClickCallback = [this, pid](UIObject *) { SelectTarget(pid); };
    root->AddChild(std::move(drag));
    if (UIObject *close = window.view.Find("btnClose", false))
      close->onClickCallback = [this, pid](UIObject *) { Close(pid); };
    if (UIObject *start = window.view.Find("btnStart", false))
      start->onClickCallback = [this, pid](UIObject *) { Start(pid); };
    if (auto *input = window.view.FindAs<UIButtonTextField>("btnInput", false)) {
      input->maxLength = 64;
      input->SetText("");
      input->onEnterCallback = [this, pid](UIObject *object) {
        HDAppTaskSnapshot state;
        if (!ReadAppTask(pid, state)) return;
        const char *display = state.name == "IP_Lookup" ? "iplookup_display" : "probe_display";
        const char *go = state.name == "IP_Lookup" ? "iplookup_go" : "probe_go";
        const std::string field = ButtonName(display, pid);
        if (SyncLegacyInput(dynamic_cast<UIButtonTextField *>(object), field.c_str())) Click(go, pid);
      };
    }
    if (UIObject *type = window.view.Find("btnType", false))
      type->onClickCallback = [pid](UIObject *) { if (SvbGetTask(pid)) Click("logmodifier_nexttype", pid); };
    if (UIObject *commit = window.view.Find("btnCommit", false))
      commit->onClickCallback = [this, pid](UIObject *) {
        auto found = windows.find(pid);
        if (found == windows.end() || !SvbGetTask(pid)) return;
        Window &window = *found->second;
        const std::string from = ButtonName("logmodifier_fromip", pid);
        const std::string to = ButtonName("logmodifier_data1", pid);
        if (SyncLegacyInput(window.view.FindAs<UIButtonTextField>("btnFrom"), from.c_str()) &&
            SyncLegacyInput(window.view.FindAs<UIButtonTextField>("btnTo"), to.c_str())) {
          Click("logmodifier_commit", pid);
          RefreshAppScheduling(pid);
        }
      };
    if (UIObject *play = window.view.Find("btnPlay", false))
      play->onClickCallback = [pid](UIObject *) { if (SvbGetTask(pid)) Click("analyser_play", pid); };
    if (UIObject *load = window.view.Find("btnLoad", false))
      load->onClickCallback = [this, pid](UIObject *) {
        if (auto *voice = dynamic_cast<VoiceAnalyser *>(SvbGetTask(pid))) {
          voice->BeginLoad();
          SelectTarget(pid);
        }
      };
    if (UIObject *save = window.view.Find("btnSave", false))
      save->onClickCallback = [this, pid](UIObject *) { SelectTarget(pid); };
    if (window.state.name == "Log_Modifier")
      if (UIObject *title = window.view.Find("txtTitle")) {
        title->interactive = true;
        title->onClickCallback = [this, pid](UIObject *) { SelectTarget(pid); };
      }
    if (window.state.name == "Trace_Tracker") {
      if (UIObject *caption = window.view.Find("txtCaption")) {
        caption->interactive = true;
        caption->onClickCallback = [pid](UIObject *) { if (SvbGetTask(pid)) Click("tracetracker_audio", pid); };
      }
    }
    if (window.state.name == "Revelation_Tracker") {
      UIObject *text = window.view.Find("txtIPs");
      if (text && text->parent == root) {
        std::unique_ptr<UIScrollableList> list(new UIScrollableList());
        list->name = "infectedScroll";
        list->x = text->x; list->y = text->y;
        list->w = text->w; list->h = text->h;
        list->scrollVertical = true; list->scrollHorizontal = false;
        std::unique_ptr<UIObject> ownedText = root->DetachChild(text);
        ownedText->x = 0; ownedText->y = 0;
        list->AddChild(std::move(ownedText));
        root->AddChild(std::move(list));
      }
    }
    if (window.state.name == "Uplink_Agent_List") {
      UIObject *text = window.view.Find("txtBox");
      UIObject *list = window.view.Find("scrollArea");
      if (text && list) {
        std::unique_ptr<UIObject> ownedText = root->DetachChild(text);
        ownedText->x = 9;
        ownedText->y = 0;
        list->AddChild(std::move(ownedText));
      }
    }
    window.wired = true;
    window.legacyNames.clear();
    const auto names = [&window, pid](std::initializer_list<const char *> prefixes) {
      for (const char *prefix : prefixes) window.legacyNames.push_back(ButtonName(prefix, pid));
    };
    const std::string &name = window.state.name;
    if (name == "Password_Breaker") names({"passwordbreaker", "passwordbreaker_fill", "passwordbreaker_close"});
    else if (name == "Dictionary_Hacker") names({"dictionaryhacker", "dictionaryhacker_b1", "dictionaryhacker_b2", "dictionaryhacker_b3", "dictionaryhacker_close"});
    else if (name == "Decypher") names({"decypher_title", "decypher_border", "decypher_progress", "decypher_close"});
    else if (name == "Firewall_Disable") names({"firewalldisable_title", "firewalldisable_border", "firewalldisable_progress", "firewalldisable_close"});
    else if (name == "Proxy_Disable") names({"proxydisable_title", "proxydisable_border", "proxydisable_progress", "proxydisable_close"});
    else if (name.find("_Bypass") != std::string::npos) names({"sbypass_main", "sbypass_pause", "sbypass_close", "sbypass_title", "sbypass_status"});
    else if (name == "IP_Lookup") names({"iplookup_go", "iplookup_display", "iplookup_close"});
    else if (name == "IP_Probe") names({"probe_go", "probe_display", "probe_close"});
    else if (name == "Log_Modifier") names({"logmodifier_title", "logmodifier_border", "logmodifier_progress", "logmodifier_close", "logmodifier_background", "logmodifier_tltype", "logmodifier_ltype", "logmodifier_tfromip", "logmodifier_fromip", "logmodifier_tdata1", "logmodifier_data1", "logmodifier_nexttype", "logmodifier_commit", "logmodifier_close2"});
    else if (name == "Voice_Analyser") names({"analyser_title", "analyser_close", "analyser_text", "analyser_analyser", "analyser_play"});
    else if (name == "Trace_Tracker") names({"tracetracker_display", "tracetracker_close", "tracetracker_audio"});
    else if (name == "Revelation_Tracker") names({"revelationtracker_title", "revelationtracker_box", "revaltiontracker_close"});
    else if (name == "Motion_Sensor") names({"motionsensor_main"});
    else if (name == "Gateway_Nuke") names({"gatenuke_title", "gatenuke_yes", "gatenuke_no"});
    else if (name == "Uplink_Agent_List") names({"uplinkagentlist", "uplinkagentlist_title", "uplinkagentlist_close", "uplinkagentlist_scrollup", "uplinkagentlist_scrolldown", "uplinkagentlist_scrollbar"});
  }
  void Render(Window &window) {
    HDLayoutView &view = window.view;
    HDAppTaskSnapshot &state = window.state;
    UIObject *root = view.Root();
    if (!root) return;
    Metadata(root, "taskPID", std::to_string(state.pid));
    Metadata(root, "taskName", state.name);
    Metadata(root, "taskVersion", std::to_string(state.version));
    Metadata(root, "taskTarget", state.target);
    Metadata(root, "taskStatus", state.statusText);
    Metadata(root, "taskDetail", state.detail);
    Metadata(root, "taskProgress", std::to_string(state.progress));
    Metadata(root, "taskPriority", std::to_string(state.priority));
    Metadata(root, "taskMinimumCPU", std::to_string(SvbGetMinimumCPUShare(state.pid)));
    SetVisible(view.Find("iconError", false), state.failed);
    SetVisible(view.Find("iconDone", false), state.complete);
    SetVisible(view.Find("iconLoading", false), state.running);
    SetVisible(view.Find("chargeBase", false), state.running);
    if (auto *arc = view.FindAs<UIArc>("chargeFill", false)) {
      arc->visible = state.running;
      arc->startAngle = -90;
      arc->endAngle = -90 + 360 * state.progress;
    }
    if (UIObject *letter = view.Find("txtLetter", false)) {
      letter->visible = state.running && !state.failed && !state.complete;
      std::string value = state.detail;
      if (auto *breaker = dynamic_cast<PasswordBreaker *>(SvbGetTask(state.pid))) {
        const int index = breaker->GetCurrentCharacter();
        value = index >= 0 && index < static_cast<int>(value.size()) ? value.substr(index, 1) : "";
      }
      SetText(letter, value);
    }
    if (UIObject *word = view.Find("txtWord", false)) {
      SetText(word, state.detail);
      word->visible = state.running;
    }
    SetVisible(view.Find("iconBypass", false), state.running);
    if (state.name == "IP_Lookup" || state.name == "IP_Probe") {
      const std::string caption = Caption(state.name == "IP_Lookup" ? "iplookup_display" : "probe_display", state.pid);
      auto *input = view.FindAs<UIButtonTextField>("btnInput");
      if (input && !input->hasFocus && !caption.empty() && caption != "Enter IP") input->SetText(caption);
    }
    if (state.name == "Log_Modifier") {
      auto *modifier = dynamic_cast<LogModifier *>(SvbGetTask(state.pid));
      const bool editing = modifier && modifier->IsExpandedInterfaceVisible();
      SetText(view.Find("txtTitle"), editing ? "Modifying Log" : "Select target log");
      SetText(view.Find("btnType"), state.detail.empty() ? "Log Type" : state.detail);
      if (editing && window.editingSource != modifier->GetSourceIndex()) {
        SetText(view.Find("btnFrom"), Caption("logmodifier_fromip", state.pid));
        SetText(view.Find("btnTo"), Caption("logmodifier_data1", state.pid));
        window.editingSource = modifier->GetSourceIndex();
      }
      if (!editing) window.editingSource = -1;
      if (auto *charge = view.FindAs<UIRectangle>("charge")) charge->w = 362 * state.progress;
    }
    if (state.name == "Voice_Analyser") {
      auto *voice = dynamic_cast<VoiceAnalyser *>(SvbGetTask(state.pid));
      const bool ready = voice && voice->GetStatus() == VOICEANALYSER_STATUS_READY;
      SetVisible(view.Find("btnStart"), !ready);
      SetVisible(view.Find("btnPlay"), ready);
      SetVisible(view.Find("btnLoad"), !state.running && state.version >= 2);
      SetVisible(view.Find("btnSave"), ready && state.version >= 2);
      SetVisible(view.Find("separator"), ready);
      SetVisible(view.Find("iconPlay"), ready);
      SetVisible(view.Find("iconApp"), !state.running && !ready);
      SetVisible(view.Find("soundBars"), state.running);
      if (voice)
        for (int i = 0; i < 7; ++i) {
          UIObject *bar = view.Find("soundBars/bar" + std::to_string(i));
          if (bar) { bar->h = static_cast<float>(voice->GetSample(i * 3)); bar->y = 40 - bar->h; }
        }
    }
    if (state.name == "Trace_Tracker") {
      Connection *connection = CurrentPlayer()->GetConnection();
      const bool traced = connection->Traced();
      const bool tracing = connection->TraceInProgress();
      SetVisible(view.Find("icon1"), !tracing && !traced);
      SetVisible(view.Find("icon2"), tracing && !traced);
      SetVisible(view.Find("icon3"), traced);
      SetText(view.Find("txtCaption"), state.version >= 3 && tracing ?
          std::to_string(CurrentPlayer()->TimeRemaining()) : state.version >= 2 && tracing ?
          std::to_string(static_cast<int>(100 * Fraction(connection->traceprogress, connection->GetSize() - 1))) + "%" : "");
      if (auto *circle = view.FindAs<UICircle>("traceGFX")) {
        circle->visible = tracing || traced;
        circle->fillColor = traced || (tracing && CurrentPlayer()->TimeRemaining() < 10) ? "D31A1A" : "FFA41C";
      }
    }
    if (state.name == "Motion_Sensor") {
      SetText(view.Find("txtDesc"), state.detail);
      const int count = std::max(0, std::min(3, state.status));
      SetImage(view.Find("icon"), "appIcon_motionSensor_0" + std::to_string(count + 1) + ".png");
    }
    if (state.name == "Revelation_Tracker") {
      std::string text;
      World *world = game->GetWorld();
      for (int i = 0; i < world->plotgenerator.infected.Size(); ++i) {
        char *ip = world->plotgenerator.infected.GetData(i);
        VLocation *location = ip ? world->GetVLocation(ip) : nullptr;
        Computer *computer = location ? location->GetComputer() : nullptr;
        if (!computer || computer->isinfected_revelation <= 0) continue;
        char line[96];
        std::snprintf(line, sizeof(line), "%s @ %02d:%02d", computer->ip,
                      computer->infectiondate.GetHour(), computer->infectiondate.GetMinute());
        if (!text.empty()) text += '\n';
        text += line;
      }
      SetText(view.Find("txtIPs"), text.empty() ? "No infected systems" : text);
      if (auto *paragraph = view.FindAs<UITextParagraph>("txtIPs")) {
        paragraph->PrepareText();
        auto *list = view.FindAs<UIScrollableList>("infectedScroll");
        ALLEGRO_FONT *font = paragraph->GetFont();
        const float advance = std::max(1.0f,
            (font ? static_cast<float>(al_get_font_line_height(font))
                  : static_cast<float>(paragraph->fontSize)) + paragraph->leading);
        paragraph->h = std::max(list ? list->h : 110.0f,
            advance * static_cast<float>(paragraph->lines.size()));
      }
    }
    if (state.name == "Uplink_Agent_List") {
      auto *agents = dynamic_cast<UplinkAgentList *>(SvbGetTask(state.pid));
      std::string text;
      if (agents) for (int i = 0; i < agents->GetAgentCount(); ++i) {
        if (!text.empty()) text += '\n';
        const char *handle = agents->GetAgentHandle(i), *name = agents->GetAgentName(i);
        text += handle ? handle : "";
        text += " : ";
        text += name ? name : "";
      }
      SetText(view.Find("scrollArea/txtBox"), text);
      if (auto *paragraph = view.FindAs<UITextParagraph>("scrollArea/txtBox")) {
        paragraph->PrepareText();
        paragraph->h = std::max(622.0f, static_cast<float>(paragraph->lines.size()) * 28);
      }
    }
    if (state.failed && view.IsVisible()) {
      if (window.error.IsVisible() || window.error.Show()) {
        window.error.Root()->x = root->x;
        window.error.Root()->y = root->y + root->h + 5;
        SetText(window.error.Find("txtCaption"), state.name == "Dictionary_Hacker" ? "Password not in dictionary" :
            state.name == "IP_Lookup" || state.name == "IP_Probe" ? "IP Not found" :
            state.name == "Password_Breaker" ? "Target cannot be cracked" :
            state.name == "Trace_Tracker" ? "Connection traced" :
            state.name == "Gateway_Nuke" ? state.statusText : "App version too low");
      }
    } else window.error.Hide();
  }
  void UpdateWindows() {
    for (auto &item : windows) item.second->seen = false;
    for (int i = 0; i < SvbNumTasks(); ++i) {
      TaskWrapper *wrapper = SvbGetTaskWrapperAtIndex(i);
      if (!wrapper || !wrapper->name) continue;
      const int pid = wrapper->pid;
      const AppDefinition *definition = Definition(wrapper->name);
      if (!definition || !definition->layout) continue;
      auto found = windows.find(pid);
      if (found == windows.end()) {
        std::unique_ptr<Window> window(new Window(owner, definition->layout));
        found = windows.emplace(pid, std::move(window)).first;
        windowsWanted = true;
      }
      Window &window = *found->second;
      window.seen = true;
      if (!ReadAppTask(pid, window.state)) continue;
      if (!window.wired || !window.view.Root()) Bind(window, pid);
      // Legacy task code requires its real buttons even while HD is hidden.
      // Do not remove those interfaces or cache their Button pointers.
      if (Task *task = SvbGetTask(pid)) if (!task->IsInterfaceVisible()) task->CreateInterface();
      if (window.view.Root()) for (const std::string &name : window.legacyNames)
        if (Button *button = LegacyFindButton(name.c_str())) {
          // Keep the real callback/caption/task state, but never draw or hit-test
          // the retired Eclipse surface underneath its final HD replacement.
          button->x = button->y = -10000;
        }
      if (windowsWanted) { if (!window.view.IsVisible()) window.view.Show(); } else window.view.Hide();
      Render(window);
    }
    for (auto item = windows.begin(); item != windows.end(); ) {
      Window &window = *item->second;
      if (!window.seen && window.terminal && EclGetAccurateTime() < window.expires && !window.closing) {
        if (!window.wired || !window.view.Root()) Bind(window, item->first);
        if (windowsWanted && !window.view.IsVisible()) window.view.Show();
        Render(window);
        ++item;
      } else if (!window.seen) item = windows.erase(item);
      else ++item;
    }
  }
  void UpdateSoftware() {
    Player *player = CurrentPlayer();
    if (!player) return;
    for (auto &item : software) item.second.seen = false;
    DataBank &bank = player->gateway.databank;
    for (int i = 0; i < bank.GetDataSize(); ++i) {
      Data *data = bank.GetDataFile(i);
      if (!data || data->TYPE != DATATYPE_PROGRAM || data->encrypted || data->compressed ||
          !Definition(data->title) || !std::strcmp(data->title, "Tutorial")) continue;
      Software &entry = software[data->title];
      if (!entry.seen || data->version > entry.version) { entry.version = data->version; entry.category = data->softwareTYPE; }
      entry.seen = true;
    }
    // HUD upgrades are installed gateway capabilities rather than data files.
    for (const auto &hud : hudApplications)
      if (player->gateway.HasHUDUpgrade(hud.upgrade)) {
        Software &entry = software[hud.name];
        entry.version = 1;
        entry.category = SOFTWARETYPE_HUDUPGRADE;
        entry.seen = true;
      }
    UIObject *list = menu.Find("appList", false);
    for (auto item = software.begin(); item != software.end(); ) {
      if (!item->second.seen) {
        if (list) list->RemoveChild(item->first);
        item = software.erase(item);
      } else ++item;
    }
    // Software and hotkey metadata remain live while the menu is hidden.
    for (auto &item : software) {
      item.second.hotkey.clear();
      if (!hotkeys) continue;
      for (const auto &binding : hotkeys->GetBindings()) {
        std::string command = binding.command;
        std::replace(command.begin(), command.end(), ' ', '_');
        if (binding.command != "Open App " + item.first &&
            command != item.first) continue;
        std::string &label = item.second.hotkey;
        if (binding.modifiers & ALLEGRO_KEYMOD_CTRL) label += "Ctrl+";
        if (binding.modifiers & ALLEGRO_KEYMOD_ALT) label += "Alt+";
        if (binding.modifiers & ALLEGRO_KEYMOD_SHIFT) label += "Shift+";
        const char *key = al_keycode_to_name(binding.key);
        if (key) label += key;
        break;
      }
    }
    if (!list) return;
    list->arrangeType = ArrangeType::ArrangeFree;
    for (const auto &child : list->children)
      if (child->name.compare(0, 9, "category:") == 0 || child->name.compare(0, 10, "separator:") == 0)
        child->visible = false;
    float y = 0;
    static const char *categories[] = {"Other", "File Utilities", "Hardware Drivers", "Security",
        "Crackers", "Bypassers", "LAN Tools", "Other", "Other", "HUD Upgrades", "Other"};
    for (int category = 0; category <= 10; ++category) {
      unsigned count = 0;
      for (const auto &item : software)
        if (std::max(0, std::min(10, item.second.category)) == category) ++count;
      if (!count) continue;
      const std::string titleName = "category:" + std::to_string(category);
      const std::string separatorName = "separator:" + std::to_string(category);
      UIObject *title = list->FindChild(titleName), *separator = list->FindChild(separatorName);
      if (!title) {
        std::unique_ptr<UIObject> text = titleFactory.Create();
        if (text) { text->name = titleName; title = text.get(); list->AddChild(std::move(text)); }
      }
      if (!separator) {
        std::unique_ptr<UIObject> line = lineFactory.Create();
        if (line) { line->name = separatorName; separator = line.get(); list->AddChild(std::move(line)); }
      }
      if (title) { title->visible = true; title->y = y + 5; SetText(title, categories[category]); }
      if (separator) { separator->visible = true; separator->y = y + 39; }
      y += 50;
      unsigned position = 0;
      for (auto &item : software) {
        Software &entry = item.second;
        if (std::max(0, std::min(10, entry.category)) != category) continue;
        UIObject *row = list->FindChild(item.first);
        if (!row) {
          std::unique_ptr<UIObject> owned = rowFactory.Create();
          if (!owned) continue;
          row = owned.get();
          row->name = item.first;
          const std::string name = item.first;
          if (UIObject *button = row->FindChild("btnBase"))
            button->onClickCallback = [this, name](UIObject *) { Launch(name, false); };
          list->AddChild(std::move(owned));
        }
        row->x = static_cast<float>((position % 2) * 240 + 4);
        row->y = y + static_cast<float>((position / 2) * 190);
        ++position;
        SetText(row->FindChild("txtName"), DisplayName(item.first, entry.version));
        const AppDefinition *definition = Definition(item.first);
        SetImage(row->FindChild("appIcon"), std::string("icon_app_") + definition->icon + ".png");
        const bool context = ValidContext(item.first);
        SetVisible(row->FindChild("iconContext"), context);
        SetVisible(row->FindChild("iconHotkey"), !entry.hotkey.empty());
        SetVisible(row->FindChild("txtHotkey"), !entry.hotkey.empty());
        SetText(row->FindChild("txtHotkey"), entry.hotkey);
        Metadata(row, "appVersion", std::to_string(entry.version));
        Metadata(row, "appContext", context ? "1" : "0");
        Metadata(row, "appName", item.first);
      }
      y += static_cast<float>((position + 1) / 2) * 190;
    }
    SetVisible(menu.Find("noApps", false), software.empty());
  }
  bool Launch(const std::string &name, bool contextual) {
    const AppDefinition *definition = Definition(name);
    const float version = OwnedVersion(name);
    if (!definition || version <= 0 || !InGame()) return false;
    if (name.compare(0, 4, "LAN_") == 0) {
      std::string command = name;
      std::replace(command.begin(), command.end(), '_', ' ');
      const bool launched =
          HDUIManager::GetInstance().InvokeContextCommand(command);
      if (launched) { menuWanted = false; menu.Hide(); }
      return launched;
    }
    if (contextual && !ValidContext(name)) return false;
    if (const HUDApplication *hud = HUDDefinition(name)) {
      if (!LegacyHasButton(hud->button) || !LegacyClick(hud->button)) return false;
      menuWanted = false;
      menu.Hide();
      return true;
    }
    int count = 0;
    for (int i = 0; i < SvbNumTasks(); ++i) {
      TaskWrapper *wrapper = SvbGetTaskWrapperAtIndex(i);
      if (wrapper && wrapper->name && name == wrapper->name) ++count;
    }
    if (count >= AppInstanceLimit(name.c_str())) {
      popup.ShowInfo("Application limit", "Too many instances of " + DisplayName(name, version));
      return true;
    }
    const int pid = LaunchOwnedApp(name);
    if (pid < 0) return false;
    SvbShowTask(pid);
    if (auto *task = dynamic_cast<UplinkTask *>(SvbGetTask(pid))) task->SetFollowMouse(false);
    menuWanted = false;
    menu.Hide();
    windowsWanted = true;
    if (contextual) Start(pid);
    SgPlaySound(RsArchiveFileOpen("sounds/runsoftware.wav"), "sounds/runsoftware.wav", false);
    return true;
  }
  bool Invoke(const std::string &command) {
    if (!InGame()) return false;
    if (command == "Open App Menu") { if (menu.IsVisible()) { menuWanted = false; menu.Hide(); return true; } return ShowMenu(); }
    int pid = -1;
    char trailing = 0;
    if (std::sscanf(command.c_str(), "Close App %d %c", &pid, &trailing) == 1 ||
        std::sscanf(command.c_str(), "Cancel App %d %c", &pid, &trailing) == 1 ||
        std::sscanf(command.c_str(), "Stop App %d %c", &pid, &trailing) == 1) {
      if (!SvbGetTask(pid) && windows.find(pid) == windows.end()) return false;
      Close(pid);
      return true;
    }
    if (std::sscanf(command.c_str(), "Start App %d %c", &pid, &trailing) == 1) return Start(pid);
    if (Definition(command)) return Launch(command, true);
    std::string name = command;
    std::replace(name.begin(), name.end(), ' ', '_');
    if (Definition(name)) return Launch(name, true);
    return false;
  }
  void Update() {
    if (!InGame()) {
      Hide();
      windows.clear();
      software.clear();
      if (UIObject *list = menu.Find("appList", false))
        if (!list->children.empty()) list->ClearChildren();
      return;
    }
    UpdateSoftware();
    UpdateWindows();
    if (menuWanted && !menu.IsVisible()) ShowMenu();
  }
};

HDAppManager::HDAppManager(UIObject &ownerRoot) : impl(new Impl(ownerRoot)) {}
HDAppManager::~HDAppManager() = default;
void HDAppManager::Update() { impl->Update(); }
void HDAppManager::Hide() { impl->Hide(); }
bool HDAppManager::IsVisible() const { return impl->Visible(); }
bool HDAppManager::ShowMenu() { return impl->ShowMenu(); }
void HDAppManager::HideMenu() { impl->menuWanted = false; impl->menu.Hide(); }
void HDAppManager::SetHotkeys(const HDHotkeyManager &hotkeys) { impl->hotkeys = &hotkeys; }
void HDAppManager::RegisterHotkeyActions(HDHotkeyManager &hotkeys) {
  for (const auto &definition : definitions) {
    const std::string name = definition.name;
    const std::string command = "Open App " + name;
    std::string context;
    if (name == "File_Copier") context = "Copy";
    else if (name == "File_Deleter") context = "Delete File";
    else if (name == "Log_Deleter") context = "Delete Log";
    else if (name == "Decrypter") context = "Decrypt";
    else if (name == "Log_UnDeleter") context = "Undo";
    else if (name == "Log_Modifier") context = "Modify";
    else if (name == "LAN_Scan") context = "LAN Scan";
    else if (name == "LAN_Probe") context = "LAN Probe";
    else if (name == "LAN_Spoof") context = "LAN Spoof";
    else if (name == "LAN_Force") context = "LAN Force";
    if (!context.empty()) {
      hotkeys.RegisterAction(command, [context] {
        return HDUIManager::GetInstance().Invoke(context);
      });
    } else {
      hotkeys.RegisterAction(command, [this, name] { return Invoke(name); });
    }
  }
}
bool HDAppManager::Launch(const std::string &name, bool contextual) { return impl->Launch(name, contextual); }
bool HDAppManager::Invoke(const std::string &command) { return impl->Invoke(command); }
bool HDAppManager::HandleInput(const ALLEGRO_EVENT &event) {
  if (impl->popup.HandleInput(event)) return true;
  if (impl->menu.IsVisible()) {
    const bool keyboard = event.type == ALLEGRO_EVENT_KEY_DOWN ||
                          event.type == ALLEGRO_EVENT_KEY_UP ||
                          event.type == ALLEGRO_EVENT_KEY_CHAR;
    if (keyboard) {
      if (event.type == ALLEGRO_EVENT_KEY_DOWN &&
          event.keyboard.keycode == ALLEGRO_KEY_ESCAPE) {
        impl->menuWanted = false;
        impl->menu.Hide();
      } else {
        impl->owner.DispatchEvent(event);
      }
      return true;
    }
  }
  if (event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN) {
    UIObject *hit = impl->owner.HitTest(static_cast<float>(event.mouse.x), static_cast<float>(event.mouse.y));
    if (hit) {
      for (auto &item : impl->windows) {
        UIObject *root = item.second->view.Root();
        if (root && (hit == root || hit->IsDescendantOf(root))) {
          item.second->view.BringToFront();
          break;
        }
      }
    }
  }
  return false;
}

} // namespace HDUI
