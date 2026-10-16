#include "hd_remote_services.h"
#include "hd_legacy_bridge.h"
#include "hd_app_windows.h"
#include "hd_atlas.h"
#include "app/app.h"
#include "app/globals.h"
#include "game/game.h"
#include "game/data/data.h"
#include "interface/interface.h"
#include "interface/remoteinterface/remoteinterface.h"
#include "interface/remoteinterface/recordscreen_interface.h"
#include "interface/remoteinterface/hwsalesscreen_interface.h"
#include "interface/remoteinterface/changegatewayscreen_interface.h"
#include "interface/remoteinterface/voiceanalysisscreen_interface.h"
#include "interface/remoteinterface/voicephonescreen_interface.h"
#include "interface/remoteinterface/radiotransmitterscreen_interface.h"
#include "interface/remoteinterface/nuclearwarscreen_interface.h"
#include "interface/taskmanager/taskmanager.h"
#include "world/world.h"
#include "world/player.h"
#include "world/agent.h"
#include "world/company/companyuplink.h"
#include "world/company/sale.h"
#include "world/computer/computer.h"
#include "world/computer/securitysystem.h"
#include "world/computer/computerscreen/genericscreen.h"
#include "soundgarden.h"
#include "redshirt.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace HDUI {
namespace {
const char *LayoutFor(int id) {
  switch (id) {
  case SCREEN_RECORDSCREEN: return "RecordScreen";
  case SCREEN_ACCOUNTSCREEN:
  case SCREEN_LOANSSCREEN: return "AccountScreen";
  case SCREEN_ACADEMICSCREEN: return "AcademicScreen";
  case SCREEN_CRIMINALSCREEN: return "CriminalScreen";
  case SCREEN_SOCSECSCREEN: return "SocialSecurityScreen";
  case SCREEN_SECURITYSCREEN: return "SecurityScreen";
  case SCREEN_SWSALESSCREEN: return "SoftwareScreen";
  case SCREEN_HWSALESSCREEN: return "HardwareScreen";
  case SCREEN_CONTACTSCREEN: return "ContactScreen";
  case SCREEN_COMPANYINFO: return "CompanyInfoScreen";
  case SCREEN_RANKINGSCREEN: return "RankingScreen";
  case SCREEN_CONSOLESCREEN: return "ConsoleScreen";
  case SCREEN_VOICEANALYSIS: return "VoiceAnalysisScreen";
  case SCREEN_VOICEPHONE: return "VoicePhoneScreen";
  case SCREEN_CHANGEGATEWAY: return "ChangeGatewayScreen";
  case SCREEN_FAITHSCREEN: return "FaithScreen";
  case SCREEN_PROTOVISION: return "ProtovisionScreen";
  case SCREEN_NUCLEARWAR: return "NuclearWarScreen";
  case SCREEN_RADIOTRANSMITTER: return "RadioTransmitterScreen";
  default: return nullptr;
  }
}

RemoteInterface *Remote() {
  if (!game || game->GameSpeed() < 0)
    return nullptr;
  RemoteInterface *remote = game->GetInterface()->GetRemoteInterface();
  return remote->IsVisible() ? remote : nullptr;
}

std::string Number(int value) { return std::to_string(value); }
std::string Indexed(const char *prefix, int value) {
  return std::string(prefix) + Number(value);
}
std::string Identity(const void *pointer) {
  char buffer[40];
  std::snprintf(buffer, sizeof(buffer), "%p", pointer);
  return buffer;
}
UIObject *Child(UIObject *root, const std::string &path) {
  return root ? root->FindChildRecursive(path) : nullptr;
}
void Text(UIObject *root, const std::string &path, const std::string &text) {
  SetText(Child(root, path), text);
}
void Visible(UIObject *root, const std::string &path, bool visible) {
  SetVisible(Child(root, path), visible);
}
void Image(UIObject *root, const std::string &path, const std::string &file) {
  auto *image = dynamic_cast<UIImage *>(Child(root, path));
  if (image && (image->file != file || !image->bitmap)) {
    image->file = file;
    image->LoadBitmap("");
  }
}
std::unique_ptr<UITextPoint> Label(const std::string &name, float x, float y,
                                 int size = 30) {
  std::unique_ptr<UITextPoint> label(new UITextPoint);
  label->name = name;
  label->x = x;
  label->y = y;
  label->font = "AeroMaticsLightRegular";
  label->fontSize = size;
  label->assetBasePath = app ? app->path : "";
  if (!label->assetBasePath.empty() && label->assetBasePath.back() != '/')
    label->assetBasePath += '/';
  label->assetBasePath += "uplinkHD/fonts";
  label->fillColor = "2BAAFF";
  return label;
}

// Protected-state adapters use pointer-to-member access, never a downcast to a
// fictitious derived object. Hardware purchases retain the real scheduled-event
// callback; software uses the final HD model path, including the extra HUD bits.
struct HardwareAccess : HWSalesScreenInterface {
  static void Select(int index) { baseoffset = 0; currentselect = index; }
};
struct GatewayAccess : ChangeGatewayScreenInterface {
  static void Select(ChangeGatewayScreenInterface &screen, int index) {
    screen.*(&GatewayAccess::currentselect) = index;
  }
};
struct RecordAccess : RecordScreenInterface {
  static int Index() { return recordindex; }
};
struct VoiceAccess : VoiceAnalysisScreenInterface {
  static int Status(VoiceAnalysisScreenInterface &screen) {
    return screen.*(&VoiceAccess::STATUS);
  }
  static int Sample(VoiceAnalysisScreenInterface &screen, int index) {
    return (screen.*(&VoiceAccess::sample))[index];
  }
};
struct RadioAccess : RadioTransmitterScreenInterface {
  static std::string Frequency() {
    return Number(frequencyGhz) + "." + Number(frequencyMhz) + " Ghz";
  }
};
struct NuclearAccess : NuclearWarScreenInterface {
  static LList<NuclearWar_Nuke *> &Nukes(NuclearWarScreenInterface &screen) {
    return screen.*(&NuclearAccess::nukes);
  }
};

int HudUpgrade(const Sale &sale) {
  if (!std::strcmp(sale.title, "HUD_MapShowTrace")) return HUDUPGRADE_MAPSHOWSTRACE;
  if (!std::strcmp(sale.title, "HUD_ConnectionAnalysis")) return HUDUPGRADE_CONNECTIONANALYSIS;
  if (!std::strcmp(sale.title, "HUD_IRC-Client")) return HUDUPGRADE_IRCCLIENT;
  if (!std::strcmp(sale.title, "HUD_LANView")) return HUDUPGRADE_LANVIEW;
  // GOLD helpers 0x46E960 / 0x46E2C0 extend the persisted HUD mask.
  if (!std::strcmp(sale.title, "HUD_ConnectionRandomizer")) return 0x10;
  if (!std::strcmp(sale.title, "HUD_TaskManager_V2.0")) return 0x20;
  return 0;
}
int SoftwareCategory(const Sale &sale) {
  switch (sale.swhwTYPE) {
  case SOFTWARETYPE_CRACKERS: return 0;
  case SOFTWARETYPE_HUDUPGRADE: return 1;
  case SOFTWARETYPE_BYPASSER: return 2;
  case SOFTWARETYPE_SECURITY: return 3;
  case SOFTWARETYPE_LANTOOL: return 4;
  case SOFTWARETYPE_OTHER: return 5;
  default: return -1;
  }
}
const char *SoftwareCategoryName(int category) {
  static const char *names[] = {"Cracker", "HUD Upgrade", "Bypasser", "Security", "LAN", "Other"};
  return names[category];
}

const char *SoftwareIcon(const Sale &sale) {
  static const char *icons[][2] = {
    {"Decrypter", "decrypter"}, {"Decypher", "decypher"},
    {"Dictionary_Hacker", "dictionaryHacker"}, {"Faith", "faith"},
    {"Firewall_Bypass", "firewallBypass"}, {"Firewall_Disable", "firewallDisable"},
    {"Gateway_Nuke", "gatewayNuke"}, {"HUD_ConnectionAnalysis", "hudConnectionAnalysis"},
    {"HUD_IRC-Client", "hudIRCClient"}, {"HUD_LANView", "hudLANView"},
    {"HUD_ConnectionRandomizer", "hudMapRandomizer"}, {"HUD_TaskManager_V2.0", "hudTaskManagerV2"},
    {"HUD_MapShowTrace", "hudMapShowTrace"}, {"IP_Lookup", "ipLookup"},
    {"IP_Probe", "ipProbe"}, {"LAN_Force", "lanForce"},
    {"LAN_Probe", "lanProbe"}, {"LAN_Scan", "lanScan"}, {"LAN_Spoof", "lanSpoof"},
    {"Log_Deleter", "logDeleter"}, {"Log_Modifier", "logModifier"},
    {"Log_UnDeleter", "logUnDeleter"}, {"Monitor_Bypass", "monitorBypass"},
    {"Motion_Sensor", "motionSensor"}, {"Password_Breaker", "passwordBreaker"},
    {"Proxy_Bypass", "proxyBypass"}, {"Proxy_Disable", "proxyDisable"},
    {"Revelation", "revelation"}, {"Revelation_Tracker", "revelationTracker"},
    {"Trace_Tracker", "traceTracker"}, {"Uplink_Agent_List", "uplinkAgentList"},
    {"Voice_Analyser", "voiceAnalyser"}
  };
  for (const auto &icon : icons)
    if (!std::strcmp(sale.title, icon[0])) return icon[1];
  return "unknown";
}

const char *HardwareIcon(const Sale &sale) {
  switch (sale.swhwTYPE) {
  case GATEWAYUPGRADETYPE_CPU: return "icon_hw_cpu.png";
  case GATEWAYUPGRADETYPE_MODEM: return "icon_hw_modem.png";
  case GATEWAYUPGRADETYPE_MEMORY: return "icon_hw_memory.png";
  case GATEWAYUPGRADETYPE_SECURITY:
    if (!std::strcmp(sale.title, "Gateway Self Destruct")) return "icon_hw_selfDestruct.png";
    if (!std::strcmp(sale.title, "Gateway Motion Sensor")) return "icon_hw_motionSensor.png";
    break;
  default: break;
  }
  return "icon_hw_unknown.png";
}

// The final software/hardware layouts use a Container for the catalogue. Turn
// that same named viewport into a scrollable list, retaining all XML geometry.
UIScrollableList *Scrollable(UIObject *root, const char *name) {
  UIObject *original = Child(root, name);
  if (auto *list = dynamic_cast<UIScrollableList *>(original))
    return list;
  if (!original || !original->parent)
    return nullptr;
  UIObject *parent = original->parent;
  std::unique_ptr<UIObject> old = parent->DetachChild(original);
  std::unique_ptr<UIScrollableList> list(new UIScrollableList);
  list->name = old->name;
  list->x = old->x; list->y = old->y; list->w = old->w; list->h = old->h;
  list->scalingType = old->scalingType;
  list->arrangeType = old->arrangeType;
  list->arrangePadding = old->arrangePadding;
  list->children = std::move(old->children);
  for (const auto &child : list->children)
    child->parent = list.get();
  UIScrollableList *result = list.get();
  parent->AddChild(std::move(list));
  return result;
}
void Reconcile(UIObject *container, const std::set<std::string> &wanted) {
  if (!container)
    return;
  for (size_t i = 0; i < container->children.size();) {
    UIObject *child = container->children[i].get();
    if (wanted.count(child->name))
      ++i;
    else
      container->DetachChild(child);
  }
  std::stable_sort(container->children.begin(), container->children.end(),
                   [](const std::unique_ptr<UIObject> &left,
                      const std::unique_ptr<UIObject> &right) {
                     return left->zOrder < right->zOrder;
                   });
  container->ArrangeChildren();
  if (auto *list = dynamic_cast<UIScrollableList *>(container))
    list->ClampScroll();
}
} // namespace

struct HDRemoteServices::State {
  explicit State(UIObject &root)
      : owner(root), popup(root), active(0), context(nullptr), page(-1),
        swCategory(0), hwType(GATEWAYUPGRADETYPE_CPU) {}

  UIObject &owner;
  HDPopupController popup;
  std::map<int, std::unique_ptr<HDLayoutView>> views;
  int active;
  ComputerScreen *context;
  int page;
  int swCategory, hwType;
  std::string selected, recordIdentity;
  std::map<std::string, int> versions;
  std::map<std::string, std::string> inputSources;
  double phoneConnectedAt = -1;
  double rankingAt = -1;
  int rankingOffset = 0;
  bool rankingInitial = true;

  HDLayoutView *View() const {
    auto found = views.find(active);
    return found == views.end() ? nullptr : found->second.get();
  }
  UIObject *Root() const { return View() ? View()->Root() : nullptr; }
  bool Current() const {
    RemoteInterface *remote = Remote();
    return remote && active && remote->GetInterfaceScreen()->ScreenID() == active &&
           remote->GetComputerScreen() == context && remote->currentscreenindex == page;
  }
  GenericScreen *Screen() const { return Current() ? dynamic_cast<GenericScreen *>(context) : nullptr; }
  Computer *ComputerModel() const { return Current() ? context->GetComputer() : nullptr; }
  CompanyUplink *Catalogue() const {
    return dynamic_cast<CompanyUplink *>(game->GetWorld()->GetCompany("Uplink"));
  }
  void Bind(const std::string &path, const std::string &command) {
    if (UIObject *object = Child(Root(), path))
      object->onClickCallback = [this, command](UIObject *) { Invoke(command); };
  }
  void Copy(const std::string &path, const std::string &legacy) {
    Text(Root(), path, LegacyCaption(legacy.c_str()));
  }
  void Input(const char *path, const char *legacy, const std::string &identity) {
    const std::string value = LegacyCaption(legacy);
    const std::string key = std::string(path) + "|" + identity;
    auto found = inputSources.find(key);
    // Do not overwrite local edits each frame; a changed record/search result
    // or a changed model caption is the authoritative refresh boundary.
    if (recordIdentity != identity || found == inputSources.end() || found->second != value)
      Text(Root(), path, value);
    inputSources[key] = value;
  }
  void Sync(const char *path, const char *legacy) {
    auto *field = dynamic_cast<UIButtonTextField *>(Child(Root(), path));
    if (field)
      SyncLegacyInput(field, legacy);
  }
  void Info(const std::string &title, const std::string &body) { popup.ShowInfo(title, body); }
  void Hide() {
    popup.Hide();
    if (View()) View()->Hide();
    active = 0;
    context = nullptr;
    page = -1;
  }

  void Prepare() {
    UIObject *root = Root();
    root->interactive = false;
    root->onClickCallback = nullptr;
    for (const char *button : {"btnOK", "btnBack", "btnDone", "btnClose", "btnHangUp"})
      Bind(button, "Back");
    Bind("btnCommit", "Commit");
    if (active == SCREEN_ACADEMICSCREEN || active == SCREEN_CRIMINALSCREEN || active == SCREEN_SOCSECSCREEN) {
      AtlasManager::GetInstance().LoadAtlas("uplinkHD_portraits_00.xml");
      AtlasManager::GetInstance().LoadAtlas("uplinkHD_portraits_01.xml");
    }
    switch (active) {
    case SCREEN_RECORDSCREEN:
      Bind("btnNext", "Next"); Bind("btnPrev", "Prev");
      break;
    case SCREEN_ACCOUNTSCREEN:
    case SCREEN_LOANSSCREEN:
      Bind("btnLoanUp", "LoanUp"); Bind("btnLoanDown", "LoanDown");
      break;
    case SCREEN_CRIMINALSCREEN:
      Bind("btnAdd", "AddConviction"); Bind("btnClear", "Clear"); Bind("btnArrest", "Arrest");
      if (auto *field = dynamic_cast<UIButtonTextField *>(Child(root, "btnRecord"))) {
        field->SetText("");
        field->onEnterCallback = [this](UIObject *) { Invoke("AddConviction"); };
      }
      break;
    case SCREEN_SWSALESSCREEN: {
      Scrollable(root, "elementsContainer");
      const char *tabs[] = {"btnCrackers", "btnHUD", "btnBypassers", "btnSecurity", "btnLAN", "btnOther"};
      for (int i = 0; i < 6; ++i) Bind(tabs[i], Indexed("SoftwareCategory:", i));
      break;
    }
    case SCREEN_HWSALESSCREEN:
      Scrollable(root, "elementsContainer");
      Bind("btnCPU", "HardwareCategory:1"); Bind("btnMemory", "HardwareCategory:4");
      Bind("btnModems", "HardwareCategory:2"); Bind("btnSecurity", "HardwareCategory:5");
      PrepareHardware(hwType);
      break;
    case SCREEN_CONTACTSCREEN:
      for (int i = 0; i < 8; ++i) Bind(Indexed("btn", i), Indexed("Question:", i));
      Visible(root, "txtSystemRef", false); Visible(root, "txtUserRef", false); Visible(root, "txtPlayerRef", false);
      if (UIObject *messages = Child(root, "messages")) messages->clipChildren = true;
      break;
    case SCREEN_COMPANYINFO:
      Bind("btnAdd1", "AddManager"); Bind("btnAdd2", "AddAdmin");
      Bind("btnCall1", "CallManager"); Bind("btnCall2", "CallAdmin");
      break;
    case SCREEN_CONSOLESCREEN:
      if (auto *field = dynamic_cast<UIButtonTextField *>(Child(root, "btnInput"))) {
        field->SetText("");
        field->maxLength = -1;
        field->onEnterCallback = [this](UIObject *) { Invoke("ConsolePost"); };
      }
      break;
    case SCREEN_VOICEANALYSIS: Bind("btnAccess", "Access"); break;
    case SCREEN_VOICEPHONE: Bind("btnRecord", "RecordVoice"); break;
    case SCREEN_PROTOVISION: {
      Bind("btnNuclear", "NuclearWar");
      UIObject *button = Child(root, "btnNuclear");
      if (button && !Child(button, "txtCaption")) {
        auto caption = Label("txtCaption", button->w / 2, 1, 24);
        caption->align = TextAlign::Center;
        button->AddChild(std::move(caption));
      }
      break;
    }
    case SCREEN_RADIOTRANSMITTER:
      Bind("btnDown", "RadioDown"); Bind("btnDown2", "RadioBigDown");
      Bind("btnUp", "RadioUp"); Bind("btnUp2", "RadioBigUp"); Bind("btnAccess", "Access");
      break;
    default: break;
    }
  }

  void PrepareHardware(int type) {
    auto *screen = dynamic_cast<HWSalesScreenInterface *>(Remote()->GetInterfaceScreen());
    if (!screen) return;
    screen->Remove();
    screen->SetHWType(type);
    screen->CreateSalesMenu(context);
    hwType = type;
  }

  void Portrait(const std::string &name) {
    Person *person = game->GetWorld()->GetPerson(const_cast<char *>(name.c_str()));
    std::string file = "person_portrait_unknown.png";
    if (person && person->photoindex > 0) {
      const std::string candidate = "person_portrait_" + Number(person->photoindex) +
                                    (person->IsFemale() ? "f.png" : "m.png");
      if (AtlasManager::GetInstance().AcquireTexture(candidate)) file = candidate;
    }
    Image(Root(), "portrait", file);
  }

  void UpdateRecords() {
    if (active == SCREEN_RECORDSCREEN) {
      // Keep the XML IDs while expanding its four example fields to the actual
      // record schema. Enter commits through the original proxy/logging path.
      UIObject *list = Child(Root(), "recordFields");
      if (!list) {
        std::unique_ptr<UIScrollableList> fields(new UIScrollableList);
        fields->name = "recordFields"; fields->x = 80; fields->y = 416;
        fields->w = 802; fields->h = 270; fields->arrangeType = ArrangeType::ArrangeVertical;
        list = fields.get();
        Root()->AddChild(std::move(fields));
        for (int i = 0; i < 4; ++i) {
          Root()->RemoveChild(Indexed("txtTitle", i));
          Root()->RemoveChild(Indexed("txtValue", i));
          Visible(Root(), Indexed("rect", i), false);
        }
      }
      std::set<std::string> wanted;
      const std::string identity = Number(RecordAccess::Index());
      for (int i = 0;; ++i) {
        const std::string legacyTitle = Indexed("recordscreen_title ", i);
        if (!LegacyHasButton(legacyTitle.c_str())) break;
        const std::string title = LegacyCaption(legacyTitle.c_str());
        const std::string key = "field:" + title;
        wanted.insert(key);
        UIObject *row = list->FindChild(key);
        if (!row) {
          std::unique_ptr<UIContainer> field(new UIContainer);
          field->name = key; field->w = 802; field->h = 67;
          field->AddChild(Label(Indexed("txtTitle", i), 160, 4));
          std::unique_ptr<UIButtonTextField> value(new UIButtonTextField);
          value->name = Indexed("txtValue", i); value->x = 480; value->y = 0;
          value->w = 320; value->h = 60;
          value->AddChild(Label("txtCaption", 0, 4));
          value->onEnterCallback = [this](UIObject *) { Invoke("Commit"); };
          field->AddChild(std::move(value));
          row = field.get(); list->AddChild(std::move(field));
        }
        row->children[0]->name = Indexed("txtTitle", i);
        row->children[1]->name = Indexed("txtValue", i);
        Text(row, Indexed("txtTitle", i), title);
        const std::string source = LegacyCaption(Indexed("recordscreen_value ", i).c_str());
        const std::string sourceKey = key + "|" + identity;
        if (recordIdentity != identity || inputSources.find(sourceKey) == inputSources.end() || inputSources[sourceKey] != source)
          Text(row, Indexed("txtValue", i), source);
        inputSources[sourceKey] = source;
        row->zOrder = i;
      }
      Reconcile(list, wanted);
      recordIdentity = identity;
      SetEnabled(Child(Root(), "btnPrev"), RecordAccess::Index() > 0);
      SetEnabled(Child(Root(), "btnNext"),
                 ComputerModel()->recordbank.GetRecord(RecordAccess::Index() + 1) != nullptr);
      return;
    }
    const char *nameButton = active == SCREEN_ACADEMICSCREEN ? "academic_name" :
                             active == SCREEN_CRIMINALSCREEN ? "criminal_name" : "ss_name";
    const std::string name = LegacyCaption(nameButton);
    Text(Root(), "txtName", name);
    Portrait(name);
    const bool writeAccess = Remote()->security_level <= 2;
    if (active == SCREEN_ACADEMICSCREEN) {
      Input("btnCollege", "academic_collegequals", name);
      Input("btnGrad", "academic_uniquals", name);
      Input("btnOther", "academic_otherquals", name);
      Visible(Root(), "btnCommit", writeAccess);
    } else if (active == SCREEN_SOCSECSCREEN) {
      Input("btnSSN", "ss_num", name); Input("btnDOB", "ss_dob", name);
      Input("btnMar", "ss_marital", name); Input("btnPer", "ss_personal", name);
      Visible(Root(), "btnCommit", writeAccess);
    } else {
      Visible(Root(), "txtRecord", false);
      UIObject *list = Child(Root(), "recordList");
      if (list && !list->FindChild("history")) {
        std::unique_ptr<UITextParagraph> history(new UITextParagraph);
        history->name = "history"; history->w = list->w; history->font = "AeroMaticsLightRegular";
        history->fontSize = 30; history->fillColor = "2BAAFF";
        if (auto *reference = dynamic_cast<UITextPoint *>(Child(Root(), "txtRecord")))
          history->assetBasePath = reference->assetBasePath;
        list->AddChild(std::move(history));
      }
      const std::string convictions = LegacyCaption("criminal_history");
      if (auto *history = dynamic_cast<UITextParagraph *>(Child(list, "history"))) {
        history->text = convictions;
        history->PrepareText();
        history->h = std::max(40.0f, float(history->lines.size()) * 36.0f);
      }
      for (const char *control : {"btnRecord", "btnAdd", "btnClear", "btnArrest"})
        Visible(Root(), control, writeAccess);
    }
    recordIdentity = name;
  }

  void UpdateAccount() {
    const bool loans = active == SCREEN_LOANSSCREEN;
    const std::string prefix = loans ? "loansscreen_" : "accountscreen_";
    Copy("txtNameVal", prefix + "accountname");
    Copy("txtNumberVal", prefix + "accountnumber");
    Text(Root(), "txtBalanceVal", LegacyCaption((prefix + "balance").c_str()) + "c");
    Text(Root(), "txtLoanVal", LegacyCaption((prefix + "loan").c_str()) + "c " + LegacyCaption((prefix + "loanrate").c_str()));
    Visible(Root(), "btnLoanUp", loans); Visible(Root(), "btnLoanDown", loans); Visible(Root(), "txtLoanMax", loans);
    if (loans) Copy("txtLoanMax", "loansscreen_maxloan");
  }

  void UpdateSecurity() {
    Computer *computer = ComputerModel();
    int shown = 0;
    for (int i = 0; i < computer->security.NumSystems(); ++i) {
      SecuritySystem *system = computer->security.GetSystem(i);
      if (!system) continue;
      const int slot = shown++;
      const std::string title = Indexed("txtSystem", slot), level = Indexed("txtLevel", slot), status = Indexed("btnStatus", slot);
      if (!Child(Root(), status)) {
        HDRepeatLayoutFactory factory("HUD/interfaces/SecurityScreen", "btnStatus0");
        std::unique_ptr<UIObject> button = factory.Create();
        if (!button) continue;
        button->name = status; button->y = 428 + slot * 90;
        Root()->AddChild(std::move(button));
        Root()->AddChild(Label(title, 159, 432 + slot * 90));
        Root()->AddChild(Label(level, 400, 432 + slot * 90));
      }
      Visible(Root(), title, true); Visible(Root(), level, true); Visible(Root(), status, true);
      Text(Root(), title, system->GetName());
      Text(Root(), level, "Level " + Number(system->level));
      Text(Root(), status, system->enabled ? "Enabled" : "Disabled");
      Visible(Root(), status + "/iconOn", system->enabled);
      Visible(Root(), status + "/iconOff", !system->enabled);
      const std::string identity = Identity(system);
      if (UIObject *button = Child(Root(), status))
        button->onClickCallback = [this, identity](UIObject *) { Invoke("Security:" + identity); };
    }
    // Additional installations are model rows too; remove them only here, never
    // from a status callback that may still be executing on a row.
    std::vector<std::string> obsolete;
    for (const auto &child : Root()->children) {
      int slot = -1;
      if (std::sscanf(child->name.c_str(), "btnStatus%d", &slot) == 1 && slot >= shown) {
        if (slot < 3) {
          Visible(Root(), Indexed("txtSystem", slot), false);
          Visible(Root(), Indexed("txtLevel", slot), false);
          child->visible = false;
        } else {
          obsolete.push_back(Indexed("txtSystem", slot)); obsolete.push_back(Indexed("txtLevel", slot)); obsolete.push_back(child->name);
        }
      }
    }
    for (const std::string &name : obsolete) Root()->RemoveChild(name);
    Visible(Root(), "txtNoSystems", shown == 0);
    const char *icons[] = {"iconProxy", "iconFirewall", "iconMonitor"};
    for (int i = 0; i < 3; ++i) {
      Visible(Root(), icons[i], i < shown);
      Visible(Root(), Indexed("rect", i), i < shown);
      if (i < shown) {
        const std::string name = GetText(Child(Root(), Indexed("txtSystem", i)));
        const char *file = name.find("Proxy") != std::string::npos ? "icon_proxy_40px.png" :
                           name.find("Firewall") != std::string::npos ? "icon_firewall_40px.png" : "icon_monitor_40px.png";
        Image(Root(), icons[i], file);
        if (name.find("Proxy") == std::string::npos &&
            name.find("Firewall") == std::string::npos &&
            name.find("Monitor") == std::string::npos)
          Visible(Root(), icons[i], false);
      }
    }
  }

  Sale *FindSale(const std::string &key, int *index = nullptr) {
    CompanyUplink *catalogue = Catalogue();
    if (!catalogue) return nullptr;
    const bool software = active == SCREEN_SWSALESSCREEN;
    const int count = software ? catalogue->sw_sales.Size() : catalogue->hw_sales.Size();
    int filtered = 0;
    for (int i = 0; i < count; ++i) {
      Sale *sale = software ? catalogue->GetSWSale(i) : catalogue->GetHWSale(i);
      if (!sale || (!software && sale->swhwTYPE != hwType)) continue;
      if (key == Identity(sale)) {
        if (index) *index = software ? i : filtered;
        return sale;
      }
      ++filtered;
    }
    return nullptr;
  }

  void UpdateSales() {
    CompanyUplink *catalogue = Catalogue();
    UIObject *list = Child(Root(), "elementsContainer");
    if (!catalogue || !list) return;
    const bool software = active == SCREEN_SWSALESSCREEN;
    HDRepeatLayoutFactory factory(software ? "HUD/interfaces/Elements/SoftwareElement" : "HUD/interfaces/Elements/HardwareElement");
    std::set<std::string> wanted;
    const int count = software ? catalogue->sw_sales.Size() : catalogue->hw_sales.Size();
    int order = 0;
    for (int i = 0; i < count; ++i) {
      Sale *sale = software ? catalogue->GetSWSale(i) : catalogue->GetHWSale(i);
      if (!sale || (software ? SoftwareCategory(*sale) != swCategory : sale->swhwTYPE != hwType)) continue;
      const std::string key = Identity(sale);
      wanted.insert(key);
      UIObject *row = list->FindChild(key);
      if (!row) {
        std::unique_ptr<UIObject> created = factory.Create();
        if (!created) continue;
        created->name = key;
        row = created.get(); list->AddChild(std::move(created));
        if (UIObject *button = Child(row, "btnBase"))
          button->onClickCallback = [this, key](UIObject *) { selected = selected == key ? "" : key; };
        const char *buy = software ? "info/btnBuy" : "btnBuy";
        if (UIObject *button = Child(row, buy))
          button->onClickCallback = [this, key](UIObject *) { Invoke("Buy:" + key); };
        if (software)
          if (UIObject *button = Child(row, "info/btnVersion"))
            button->onClickCallback = [this, key](UIObject *) { Invoke("Version:" + key); };
      }
      row->zOrder = order++;
      int &version = versions[key];
      if (version < 1 || !sale->GetVersion(version)) version = 1;
      SaleVersion *offer = sale->GetVersion(software ? version : 1);
      if (!offer) continue;
      Text(row, "txtName", sale->title); Text(row, "info/txtName", sale->title);
      Text(row, "info/txtDesc", offer->GetDetails());
      Text(row, software ? "info/txtPrice" : "txtPrice", Number(offer->cost) + "c");
      Visible(row, "info", selected == key);
      if (software) {
        Text(row, "txtType", SoftwareCategoryName(SoftwareCategory(*sale)));
        Text(row, "info/btnVersion", "v" + Number(offer->data) + ".0");
        Visible(row, "info/canChangeVs", sale->GetVersion(2) != nullptr);
        Image(row, "appIcon", std::string("icon_app_") + SoftwareIcon(*sale) + ".png");
      } else {
        Image(row, "hwIcon", HardwareIcon(*sale));
      }
      // Unaffordable items remain actionable: the real market callback explains
      // the failure instead of silently disabling a final purchase control.
      row->alpha = game->GetWorld()->GetPlayer()->GetBalance() >= offer->cost ? 1.0f : 0.55f;
    }
    Reconcile(list, wanted);
    if (!selected.empty() && !wanted.count(selected)) selected.clear();
    UIObject *marker = Child(Root(), "selectionMarker");
    if (marker) {
      if (software) marker->x = 121 + swCategory * 120;
      else {
        const int column = hwType == GATEWAYUPGRADETYPE_CPU ? 0 : hwType == GATEWAYUPGRADETYPE_MEMORY ? 1 : hwType == GATEWAYUPGRADETYPE_MODEM ? 2 : 3;
        marker->x = 160 + column * 160;
      }
    }
  }

  void Buy(const std::string &key) {
    int index = -1;
    Sale *sale = FindSale(key, &index);
    if (!sale) return;
    selected = key;
    Player *player = game->GetWorld()->GetPlayer();
    Gateway &gateway = player->gateway;
    if (active == SCREEN_SWSALESSCREEN) {
      const int version = versions[key] > 0 ? versions[key] : 1;
      SaleVersion *offer = sale->GetVersion(version);
      if (!offer) return;
      const bool hud = sale->swhwTYPE == SOFTWARETYPE_HUDUPGRADE;
      Data candidate;
      const std::string failure = std::string("Can't buy ") + sale->title + ".\n";
      candidate.SetTitle(sale->title);
      candidate.SetDetails(DATATYPE_PROGRAM, offer->size, 0, 0, float(offer->data), sale->swhwTYPE);
      const int placement = hud ? -1 : gateway.databank.FindValidPlacement(&candidate);
      if (!hud && placement < 0) {
        Info("ERROR", failure + "There is not enough disk space on your Gateway. Please remove some files and then try again."); return;
      }
      if (player->GetBalance() < offer->cost) {
        Info("ERROR", failure + "Your purchase was rejected because of insufficient funds. Please review your account and then try again."); return;
      }
      if (sale->swhwTYPE == SOFTWARETYPE_BYPASSER && !gateway.HasHUDUpgrade(HUDUPGRADE_CONNECTIONANALYSIS)) {
        Info("ERROR", failure + "You require a HUD_ConnectionAnalysis upgrade before purchasing any bypasser."); return;
      }
      if (sale->swhwTYPE == SOFTWARETYPE_LANTOOL && !gateway.HasHUDUpgrade(HUDUPGRADE_LANVIEW)) {
        Info("ERROR", failure + "You require a HUD_LANView upgrade before purchasing any Lan Tool."); return;
      }
      const int upgrade = HudUpgrade(*sale);
      bool owned = hud && upgrade && gateway.HasHUDUpgrade(upgrade);
      if (!hud)
        for (int i = 0; i < gateway.databank.GetDataSize(); ++i) {
          Data *installed = gateway.databank.GetDataFile(i);
          if (installed && installed->TYPE == DATATYPE_PROGRAM &&
              !std::strcmp(installed->title, sale->title) &&
              installed->version == float(offer->data)) {
            owned = true; break;
          }
        }
      if (owned) {
        Info("ERROR", std::string("You appear to already have ") + sale->title + ". You won't be charged."); return;
      }
      if (hud) {
        if (!upgrade) {
          Info("ERROR", "Unrecognised HUD upgrade"); return;
        }
        gateway.GiveHUDUpgrade(upgrade);
      } else {
        gateway.databank.PutData(new Data(&candidate), placement);
      }
      player->ChangeBalance(-offer->cost, "Uplink Corporation Software sales.");
      SgPlaySound(RsArchiveFileOpen("uplinkHD/sounds/buy.wav"), "uplinkHD/sounds/buy.wav", false);
      Info("SUCCESS", std::string("Purchase successful.\nThe product: ") + sale->title +
                      ", has already been downloaded and installed on your Gateway.");
    } else {
      SaleVersion *offer = sale->GetVersion(1);
      if (!offer || !gateway.curgatewaydef) return;
      const std::string failure = std::string("Can't buy ") + sale->title + ".\n";
      std::string error;
      if (player->GetBalance() < offer->cost)
        error = "Your purchase was rejected because of insufficient funds. Please review your account and then try again.";
      else if (sale->swhwTYPE == GATEWAYUPGRADETYPE_MEMORY &&
               gateway.memorysize + offer->data > gateway.curgatewaydef->maxmemory * 8)
        error = "There are not enough memory slots on your Gateway. Please upgrade your Gateway and then try again.";
      else if (sale->swhwTYPE == GATEWAYUPGRADETYPE_SECURITY) {
        if (gateway.GetNumSecurity() + 1 > gateway.curgatewaydef->maxsecurity)
          error = "There are not enough security slots on your Gateway. Please upgrade your Gateway and then try again.";
        else if (!std::strcmp(sale->title, "Gateway Self Destruct") ||
                 !std::strcmp(sale->title, "Gateway Motion Sensor")) {
          int used = 0;
          for (int i = 0; i < gateway.databank.GetDataSize(); ++i)
            if (Data *data = gateway.databank.GetDataFile(i)) used += data->size;
          if (used >= gateway.databank.GetSize())
            error = "There is not enough disk space on your Gateway for the driver (1 GQ). Please remove some files and then try again.";
        }
      } else if (sale->swhwTYPE == GATEWAYUPGRADETYPE_MODEM &&
                 gateway.curgatewaydef->bandwidth < offer->data)
        error = "Your gateway does not have sufficient bandwidth to run a modem of that speed.";
      HardwareAccess::Select(index);
      if (error.empty()) {
        const std::string result = std::string("The item ") + sale->title +
          " has been purchased and will be installed in the next 24 hours.\nYou will be notified when the job is completed.";
        LegacyClick("hwsales_accept");
        SgPlaySound(RsArchiveFileOpen("uplinkHD/sounds/buy.wav"), "uplinkHD/sounds/buy.wav", false);
        Info("SUCCESS", result);
      } else {
        LegacyClick("hwsales_accept");
        Info("ERROR", failure + error);
      }
    }
  }

  void UpdateContact() {
    Copy("title/txtContactTitle", "contact_title");
    UIObject *messages = Child(Root(), "messages");
    for (int i = 0; i < 14; ++i) {
      const std::string name = Indexed("line", i);
      if (messages && !messages->FindChild(name)) {
        auto text = Label(name, 0, float(i * 43), 24);
        messages->AddChild(std::move(text));
      }
      const std::string message = LegacyCaption(Indexed("contact_text ", i).c_str());
      Text(messages, name, message);
      if (auto *text = dynamic_cast<UITextPoint *>(Child(messages, name))) {
        const std::string playerPrefix = std::string(game->GetWorld()->GetPlayer()->handle) + " : ";
        const bool player = message.compare(0, playerPrefix.size(), playerPrefix) == 0;
        text->fillColor = player ? "2BAAFF" : message.find(" : ") != std::string::npos ? "FFFFFF" : "1C5B9B";
        text->align = player ? TextAlign::Right : TextAlign::Left;
        text->x = player && messages ? messages->w : 0;
      }
    }
    for (int i = 0; i < 8; ++i) {
      const std::string legacy = Indexed("contact_question ", i);
      Visible(Root(), Indexed("btn", i), LegacyHasButton(legacy.c_str()));
      Copy(Indexed("btn", i), legacy);
    }
  }

  void UpdateCompany() {
    auto mailPart = [](const char *name) {
      std::string part(name ? name : "");
      for (char &character : part)
        if (character == ' ' || character == '.' || character == '\'') character = '_';
      return part;
    };
    Company *company = game->GetWorld()->GetCompany(ComputerModel()->companyname);
    Person *manager = company ? game->GetWorld()->GetPerson(company->boss) : nullptr;
    Person *admin = company ? game->GetWorld()->GetPerson(company->administrator) : nullptr;
    Text(Root(), "txtManagerName", company ? company->boss : "Unlisted");
    Text(Root(), "txtAdminName", company ? company->administrator : "Unlisted");
    const bool listed = manager && admin && company;
    Text(Root(), "txtManagerMail", listed ? mailPart(manager->name) + "@" + mailPart(company->name) + ".net" : "Unlisted");
    Text(Root(), "txtAdminMail", listed ? mailPart(admin->name) + "@" + mailPart(company->name) + ".net" : "Unlisted");
    Text(Root(), "txtManagerPhone", listed ? std::string("tel. ") + manager->phonenumber : "Unlisted");
    Text(Root(), "txtAdminPhone", listed ? std::string("tel. ") + admin->phonenumber : "Unlisted");
    Person *contacts[] = {manager, admin};
    for (int i = 0; i < 2; ++i) {
      Person *person = contacts[i];
      const bool valid = person && std::strcmp(person->phonenumber, " ") && game->GetWorld()->GetVLocation(person->phonenumber);
      Visible(Root(), Indexed("btnCall", i + 1), valid);
      Visible(Root(), Indexed("btnAdd", i + 1), valid && !game->GetWorld()->GetPlayer()->HasLink(person->phonenumber));
    }
    const bool canClose = LegacyHasButton("companyscreen_back");
    Visible(Root(), "btnBack", canClose);
    Visible(Root(), "btnClose", canClose);
  }

  void UpdateRanking() {
    const double now = EclGetAccurateTime();
    if (!rankingInitial && now < rankingAt + 5000) return;
    rankingAt = now;
    std::vector<Agent *> ranked;
    std::unique_ptr<DArray<Person *>> people(game->GetWorld()->people.ConvertToDArray());
    for (int i = 0; i < people->Size(); ++i)
      if (people->ValidIndex(i))
        if (auto *agent = dynamic_cast<Agent *>(people->GetData(i)))
          ranked.push_back(agent);
    // Legacy inserts equal scores before the previous row.
    std::reverse(ranked.begin(), ranked.end());
    std::stable_sort(ranked.begin(), ranked.end(), [](Agent *left, Agent *right) {
      return left->rating.uplinkscore > right->rating.uplinkscore;
    });
    Player *player = game->GetWorld()->GetPlayer();
    auto playerPosition = std::find(ranked.begin(), ranked.end(), player);
    const int playerRank = int(playerPosition - ranked.begin());
    if (rankingInitial) {
      int legacyRank = 0;
      const std::string oldRank = LegacyCaption("ranking_playerrank");
      if (playerRank >= 10 && std::sscanf(oldRank.c_str(), "%d", &legacyRank) == 1)
        rankingOffset = std::max(0, legacyRank - playerRank - 1);
      rankingInitial = false;
    }
    auto status = [](Agent *agent) -> const char * {
      switch (agent->GetStatus()) {
      case PERSON_STATUS_NONE: return "Active";
      case PERSON_STATUS_INJAIL: return "In Jail";
      case PERSON_STATUS_DEAD: return "Deceased";
      default: return "Unknown";
      }
    };
    for (int i = 0; i < 10; ++i) {
      const bool valid = i < int(ranked.size());
      Visible(Root(), Indexed("txtName", i), valid);
      Visible(Root(), Indexed("txtStatus", i), valid);
      if (!valid) continue;
      Agent *agent = ranked[i];
      Text(Root(), Indexed("txtName", i), Number(i + 1) + " : " + agent->handle);
      Text(Root(), Indexed("txtStatus", i), status(agent));
      if (auto *name = dynamic_cast<UITextPoint *>(Child(Root(), Indexed("txtName", i))))
        name->alpha = agent->GetStatus() == PERSON_STATUS_NONE ? 1.0f : 0.5f;
    }
    const bool separate = playerPosition != ranked.end() && playerRank >= 10;
    Text(Root(), "txtNamePlayer", Number(playerRank + rankingOffset + 1) + " : " + player->handle);
    Text(Root(), "txtStatusPlayer", status(player));
    for (const char *path : {"txtNamePlayer", "txtStatusPlayer", "playerBg", "separator"})
      Visible(Root(), path, separate);
  }

  void UpdateGateways() {
    auto *screen = dynamic_cast<ChangeGatewayScreenInterface *>(Remote()->GetInterfaceScreen());
    UIObject *list = Child(Root(), "elementsContainer");
    if (!screen || !list) return;
    HDRepeatLayoutFactory factory("HUD/interfaces/Elements/GatewayElement");
    std::set<std::string> wanted;
    for (int i = 0; i < game->GetWorld()->gatewaydefs.Size(); ++i) {
      if (!game->GetWorld()->gatewaydefs.ValidIndex(i)) continue;
      GatewayDef *gateway = game->GetWorld()->gatewaydefs.GetData(i);
      if (!gateway) continue;
      const std::string key = Identity(gateway);
      wanted.insert(key);
      UIObject *row = list->FindChild(key);
      if (!row) {
        auto created = factory.Create();
        if (!created) continue;
        created->name = key;
        row = created.get(); list->AddChild(std::move(created));
        if (UIObject *button = Child(row, "btnBase"))
          button->onClickCallback = [this, key](UIObject *) { selected = selected == key ? "" : key; };
        if (UIObject *button = Child(row, "btnBuy"))
          button->onClickCallback = [this, key](UIObject *) { Invoke("Gateway:" + key); };
      }
      const int price = screen->GetGatewayPrice(i);
      Text(row, "txtName", gateway->name); Text(row, "info/txtName", gateway->name);
      Text(row, "txtPrice", Number(price) + "c"); Text(row, "info/txtPrice", Number(price) + "c");
      Text(row, "info/txtDesc", gateway->description);
      Text(row, "info/txtCPU", "Max CPUs: " + Number(gateway->maxcpus));
      Text(row, "info/txtMemory", "Max Memory: " + Number(gateway->maxmemory * 8) + " GQ");
      Text(row, "info/txtSecurity", "Max Security: " + Number(gateway->maxsecurity));
      Text(row, "info/txtBandwidth", "Bandwidth: " + Number(gateway->bandwidth) + " GQ");
      Visible(row, "info", selected == key);
      row->alpha = game->GetWorld()->GetPlayer()->GetBalance() >= price ? 1.0f : 0.55f;
      row->zOrder = i;
    }
    Reconcile(list, wanted);
    if (!selected.empty() && !wanted.count(selected)) selected.clear();
  }

  void ChooseGateway(const std::string &key) {
    auto *screen = dynamic_cast<ChangeGatewayScreenInterface *>(Remote()->GetInterfaceScreen());
    if (!screen || !LegacyHasButton("changegateway_picture")) return;
    for (int i = 0; i < game->GetWorld()->gatewaydefs.Size(); ++i) {
      if (!game->GetWorld()->gatewaydefs.ValidIndex(i)) continue;
      GatewayDef *gateway = game->GetWorld()->gatewaydefs.GetData(i);
      if (!gateway || Identity(gateway) != key) continue;
      const int price = screen->GetGatewayPrice(i);
      if (game->GetWorld()->GetPlayer()->GetBalance() < price) {
        Info("Gateway exchange", "You do not have enough credits to purchase this gateway."); return;
      }
      if (gateway == game->GetWorld()->GetPlayer()->gateway.curgatewaydef) {
        Info("Gateway exchange", "You already own this gateway."); return;
      }
      selected = key;
      screen->ShowGateway(i);
      LegacyClick("changegateway_buy");
      if (!LegacyHasButton("changegateway_picture") && LegacyHasButton("changegateway_buy")) {
        std::string body = std::string("Part exchange your current gateway for a ") + gateway->name +
          ".\nUplink Corporation will copy your hardware and memory banks. Installation takes around 24 hours, after which you must log off and back on. Your old gateway will be dismantled.\n\n";
        if (price < 0) body += "Downgrading may lose software or hardware.\n";
        body += price > 0 ? "This exchange will cost " + Number(price) + " credits." : "You will receive " + Number(-price) + " credits.";
        popup.ShowConfirm("Gateway exchange", body,
          [this, key] {
            if (!Current()) return;
            auto *live = dynamic_cast<ChangeGatewayScreenInterface *>(Remote()->GetInterfaceScreen());
            if (!live) return;
            for (int j = 0; j < game->GetWorld()->gatewaydefs.Size(); ++j)
              if (game->GetWorld()->gatewaydefs.ValidIndex(j) && Identity(game->GetWorld()->gatewaydefs.GetData(j)) == key) {
                GatewayAccess::Select(*live, j);
                if (game->GetWorld()->GetPlayer()->GetBalance() >= live->GetGatewayPrice(j)) LegacyClick("changegateway_buy");
                else { LegacyClick("changegateway_close"); Info("Gateway exchange", "You no longer have enough credits for this exchange."); }
                return;
              }
            LegacyClick("changegateway_close");
          },
          [this] { if (Current()) LegacyClick("changegateway_close"); });
      }
      return;
    }
  }

  void UpdateNuclear() {
    auto *screen = dynamic_cast<NuclearWarScreenInterface *>(Remote()->GetInterfaceScreen());
    if (!screen) return;
    const bool defcon = game->GetWorldMapType() == Game::defconworldmap;
    const int count = defcon ? NUM_PHYSICALGATEWAYLOCATIONS_DEFCON : NUM_PHYSICALGATEWAYLOCATIONS;
    UIObject *map = Child(Root(), "map");
    if (!map) return;
    Button *legacyMap = LegacyFindButton("nuclearwar_largemap");
    if (!legacyMap || !legacyMap->width || !legacyMap->height) return;
    map->w = 930;
    map->h = 360.0f * 930.0f / 641.0f;
    for (int i = 0; i < count; ++i) {
      const PhysicalGatewayLocation &location = defcon ? PHYSICALGATEWAYLOCATIONS_DEFCON[i] : PHYSICALGATEWAYLOCATIONS[i];
      const std::string name = Indexed("btnLoc", i);
      if (!Child(Root(), name)) {
        HDRepeatLayoutFactory factory("HUD/interfaces/NuclearWarScreen", "btnLoc0");
        auto button = factory.Create();
        if (!button) continue;
        button->name = name; Root()->AddChild(std::move(button));
        Root()->AddChild(Label(Indexed("txtLoc", i), 0, 0, 18));
      }
      // Location identities come from the active world map, not the eight
      // example cities embedded in the final XML.
      if (UIObject *button = Child(Root(), name)) {
        button->visible = true;
        Button *legacyLocation = LegacyFindButton(Indexed("nuclearwar_location ", i).c_str());
        if (!legacyLocation) { button->visible = false; continue; }
        button->x = map->x + (legacyLocation->x - legacyMap->x) * map->w / legacyMap->width;
        button->y = map->y + (legacyLocation->y - legacyMap->y) * map->h / legacyMap->height;
        button->onClickCallback = [this, i](UIObject *) { Invoke(Indexed("Nuke:", i)); };
        if (UIObject *label = Child(Root(), Indexed("txtLoc", i))) {
          label->visible = true; label->x = button->x - 7; label->y = button->y + 18;
          SetText(label, location.city);
        }
      }
    }
    for (int i = count; i < 8; ++i) {
      Visible(Root(), Indexed("btnLoc", i), false); Visible(Root(), Indexed("txtLoc", i), false);
    }
    UIObject *effects = Child(Root(), "explosions");
    Button *oldMap = LegacyFindButton("nuclearwar_largemap");
    if (!effects || !oldMap || !oldMap->width || !oldMap->height) return;
    const float sx = map->w / oldMap->width, sy = map->h / oldMap->height;
    std::set<std::string> wanted;
    auto &nukes = NuclearAccess::Nukes(*screen);
    for (int i = 0; i < nukes.Size(); ++i) {
      NuclearWar_Nuke *nuke = nukes.GetData(i);
      if (!nuke) continue;
      const int age = int(EclGetAccurateTime()) - nuke->time;
      if (age > 10000) {
        delete nuke; nukes.RemoveData(i--); continue;
      }
      const std::string key = Identity(nuke);
      wanted.insert(key);
      UIObject *effect = effects->FindChild(key);
      if (!effect) {
        std::unique_ptr<UIContainer> created(new UIContainer);
        created->name = key;
        std::unique_ptr<UILine> trail(new UILine); trail->name = "trail"; trail->strokeWidth = 2;
        created->AddChild(std::move(trail));
        std::unique_ptr<UICircle> blast(new UICircle); blast->name = "blast"; blast->subtype = "filled";
        created->AddChild(std::move(blast));
        effect = created.get(); effects->AddChild(std::move(created));
      }
      const float startX = map->x + (nuke->sx - oldMap->x) * sx;
      const float startY = map->y + (nuke->sy - oldMap->y) * sy;
      const float endX = map->x + nuke->x * sx, endY = map->y + nuke->y * sy;
      const bool exploded = age > 3000;
      const float fraction = exploded ? (age - 3000) / 7000.0f : age / 3000.0f;
      auto *trail = dynamic_cast<UILine *>(Child(effect, "trail"));
      auto *blast = dynamic_cast<UICircle *>(Child(effect, "blast"));
      if (trail) {
        trail->x1 = exploded ? startX + (endX - startX) * fraction : startX;
        trail->y1 = exploded ? startY + (endY - startY) * fraction : startY;
        trail->x2 = exploded ? endX : startX + (endX - startX) * fraction;
        trail->y2 = exploded ? endY : startY + (endY - startY) * fraction;
        trail->strokeColor = exploded ? "FF0000" : "FFFFFF";
        trail->alpha = exploded ? 1.0f - fraction : fraction;
      }
      if (blast) {
        blast->visible = exploded;
        blast->w = blast->h = 50 * (1.0f - fraction);
        blast->x = endX - blast->w / 2; blast->y = endY - blast->h / 2;
        blast->fillColor = age < 3200 ? "FFCC00" : "FF0000";
        blast->alpha = 1.0f - fraction;
      }
      if (exploded && !nuke->sound) {
        SgPlaySound(RsArchiveFileOpen("sounds/explosion1.wav"), "sounds/explosion1.wav", false);
        nuke->sound = true;
      }
    }
    Reconcile(effects, wanted);
  }

  void UpdateConsole() {
    std::string transcript;
    for (int index = 0; index < 21; ++index) {
      const std::string line =
          LegacyCaption(Indexed("console_text ", index).c_str());
      if (!transcript.empty())
        transcript += '\n';
      transcript += line;
    }
    Text(Root(), "txtConsole", transcript);
    const std::string legacy = LegacyCaption("console_typehere");
    const std::size_t marker = legacy.find('>');
    Text(Root(), "txtDir",
         marker == std::string::npos ? legacy : legacy.substr(0, marker + 1));
  }

  void UpdateSpecial() {
    switch (active) {
    case SCREEN_FAITHSCREEN: {
      char faith[64], revelation[64];
      std::snprintf(faith, sizeof(faith), "Version %.1f", game->GetWorld()->plotgenerator.GetVersion_Faith());
      std::snprintf(revelation, sizeof(revelation), "Version %.1f", game->GetWorld()->plotgenerator.GetVersion_Revelation());
      Text(Root(), "txtFaithV", faith); Text(Root(), "txtRevelationV", revelation);
      break;
    }
    case SCREEN_PROTOVISION:
      Copy("txtDesc", "protovisionscreen_message");
      Text(Root(), "btnNuclear", "Global Thermonuclear War");
      Visible(Root(), "btnNuclear", EclIsCaptionChangeActive("protovisionscreen_message") < 0);
      break;
    case SCREEN_RADIOTRANSMITTER: {
      Text(Root(), "txtFreq", RadioAccess::Frequency());
      Copy("txtHelp", "radioscreen_text"); Copy("txtStatus", "radioscreen_statustext");
      const bool idle = LegacyCaption("radioscreen_statustext") == "Not Connected";
      Visible(Root(), "txtStatus", !idle);
      Visible(Root(), "iconStatus", !idle);
      for (const char *control : {"btnAccess", "btnDown", "btnDown2", "btnUp", "btnUp2", "txtFreq", "txtTitleFreq"})
        Visible(Root(), control, idle);
      break;
    }
    case SCREEN_VOICEANALYSIS: {
      auto *screen = dynamic_cast<VoiceAnalysisScreenInterface *>(Remote()->GetInterfaceScreen());
      if (!screen) break;
      const int status = VoiceAccess::Status(*screen);
      const bool passed = status == VOICEANALYSISSCREEN_PASSED;
      Text(Root(), "btnAccess", passed ? "Access Granted" : "Access Denied");
      SetEnabled(Child(Root(), "btnAccess"), passed);
      Visible(Root(), "iconAnalyse", status == VOICEANALYSISSCREEN_ANALYSING);
      Visible(Root(), "soundSynths", status != VOICEANALYSISSCREEN_ANALYSING);
      for (int i = 0; i < 12; ++i)
        if (UIObject *bar = Child(Root(), "soundSynths/" + Indexed("synth", i))) {
          bar->h = VoiceAccess::Sample(*screen, i * (VOICE_NUMSAMPLES - 1) / 11) * 94.0f / 40.0f;
          bar->y = 94 - bar->h;
        }
      break;
    }
    case SCREEN_VOICEPHONE: {
      auto *phone = dynamic_cast<VoicePhoneScreenInterface *>(Remote()->GetInterfaceScreen());
      Person *person = game->GetWorld()->GetPerson(ComputerModel()->companyname);
      Text(Root(), "txtName", person ? person->name : ComputerModel()->companyname);
      Text(Root(), "txtNumber", person ? person->phonenumber : ComputerModel()->ip);
      const bool ended = phone && phone->STATUS == VOICESTATUS_HUNGUP;
      const bool connected = phone && (phone->STATUS == VOICESTATUS_CONNECTED || phone->STATUS == VOICESTATUS_TALKING);
      if (connected && phoneConnectedAt < 0) phoneConnectedAt = EclGetAccurateTime();
      if (ended) {
        Text(Root(), "txtStatus", phoneConnectedAt < 0 ? "No answer" : "Call ended");
      } else if (connected) {
        const int seconds = std::max(0, int((EclGetAccurateTime() - phoneConnectedAt) / 1000));
        char duration[32];
        std::snprintf(duration, sizeof(duration), "%02d:%02d", seconds / 60, seconds % 60);
        Text(Root(), "txtStatus", duration);
      } else {
        Text(Root(), "txtStatus", "Calling...");
      }
      bool executable = false;
      DataBank &bank = game->GetWorld()->GetPlayer()->gateway.databank;
      for (int i = 0; i < bank.GetDataSize(); ++i) {
        Data *program = bank.GetDataFile(i);
        if (program && program->TYPE == DATATYPE_PROGRAM &&
            !std::strcmp(program->title, "Voice_Analyser") && program->version > 0 &&
            !program->encrypted && !program->compressed) {
          executable = true; break;
        }
      }
      SetEnabled(Child(Root(), "btnRecord"), !ended && executable);
      SetEnabled(Child(Root(), "btnHangUp"), !ended);
      break;
    }
    case SCREEN_NUCLEARWAR: UpdateNuclear(); break;
    default: break;
    }
  }

  void Update() {
    RemoteInterface *remote = Remote();
    const int id = remote ? remote->GetInterfaceScreen()->ScreenID() : 0;
    if (!LayoutFor(id)) { Hide(); return; }
    ComputerScreen *screen = remote->GetComputerScreen();
    if (active != id || context != screen || page != remote->currentscreenindex) {
      Hide();
      active = id; context = screen; page = remote->currentscreenindex;
      selected.clear(); recordIdentity.clear(); inputSources.clear();
      phoneConnectedAt = -1; rankingAt = -1; rankingInitial = true; rankingOffset = 0;
      if (!views.count(id))
        views[id].reset(new HDLayoutView(owner, std::string("HUD/interfaces/") + LayoutFor(id)));
      View()->SetCaptureInput(false);
      if (!View()->Show()) { Hide(); return; }
      Prepare();
    } else if (!View()->IsVisible()) {
      if (!View()->Show()) return;
    }
    if (auto *generic = dynamic_cast<GenericScreen *>(context)) {
      Text(Root(), "txtTitle", generic->maintitle);
      Text(Root(), "txtSubtitle", generic->subtitle);
    }
    switch (active) {
    case SCREEN_RECORDSCREEN:
    case SCREEN_ACADEMICSCREEN:
    case SCREEN_CRIMINALSCREEN:
    case SCREEN_SOCSECSCREEN: UpdateRecords(); break;
    case SCREEN_ACCOUNTSCREEN:
    case SCREEN_LOANSSCREEN: UpdateAccount(); break;
    case SCREEN_SECURITYSCREEN: UpdateSecurity(); break;
    case SCREEN_SWSALESSCREEN:
    case SCREEN_HWSALESSCREEN: UpdateSales(); break;
    case SCREEN_CONTACTSCREEN: UpdateContact(); break;
    case SCREEN_COMPANYINFO: UpdateCompany(); break;
    case SCREEN_RANKINGSCREEN: UpdateRanking(); break;
    case SCREEN_CONSOLESCREEN: UpdateConsole(); break;
    case SCREEN_CHANGEGATEWAY: UpdateGateways(); break;
    default: UpdateSpecial(); break;
    }
  }

  bool Back() {
    switch (active) {
    case SCREEN_RECORDSCREEN:
      if (auto *screen = Screen()) {
        const std::string name = "recordscreen_click " + Number(screen->nextpage) + " " + screen->GetComputer()->ip;
        return LegacyClick(name.c_str());
      }
      return false;
    case SCREEN_ACCOUNTSCREEN: return LegacyClick("accountscreen_close");
    case SCREEN_LOANSSCREEN: return LegacyClick("loansscreen_close");
    case SCREEN_ACADEMICSCREEN: return LegacyClick("academic_close");
    case SCREEN_CRIMINALSCREEN: return LegacyClick("criminal_close");
    case SCREEN_SOCSECSCREEN: return LegacyClick("ss_close");
    case SCREEN_SECURITYSCREEN: return LegacyClick("securityscreen_close");
    case SCREEN_SWSALESSCREEN: return LegacyClick("swsales_exit");
    case SCREEN_HWSALESSCREEN:
      // GOLD's category tabs replace the old intermediate hardware menu.
      if (LegacyHasButton("hwsales_accept")) LegacyClick("hwsales_exit");
      return LegacyClick("hwsales_exit");
    case SCREEN_CONTACTSCREEN:
      if (LegacyHasButton("contact_question 7")) return LegacyClick("contact_question 7");
      if (auto *screen = Screen()) {
        if (screen->nextpage < 0) return false;
        Remote()->RunScreen(screen->nextpage, screen->GetComputer());
        return true;
      }
      return false;
    case SCREEN_COMPANYINFO: return LegacyClick("companyscreen_back");
    case SCREEN_RANKINGSCREEN: return LegacyClick("ranking_exit");
    case SCREEN_CHANGEGATEWAY: return LegacyClick("changegateway_close");
    case SCREEN_FAITHSCREEN: return LegacyClick("faithscreen_close");
    case SCREEN_PROTOVISION:
      if (auto *screen = Screen()) {
        const std::string name = "protovisionscreen_close " + Number(screen->nextpage) + " " + screen->GetComputer()->ip;
        return LegacyClick(name.c_str());
      }
      return false;
    case SCREEN_NUCLEARWAR: return LegacyClick("nuclearwar_close");
    case SCREEN_VOICEPHONE: {
      RemoteInterface *remote = Remote();
      Connection *connection = game->GetWorld()->GetPlayer()->GetConnection();
      connection->Disconnect(); connection->Reset(); remote->RunNewLocation();
      return true;
    }
    default: return false;
    }
  }

  bool Invoke(const std::string &command) {
    if (!Current()) return false;
    if (popup.IsVisible()) {
      if (command == "Accept") { popup.Accept(); return true; }
      if (command == "Cancel") { popup.Cancel(); return true; }
      return false;
    }
    if (active == SCREEN_CONSOLESCREEN && command == "ConsolePost") {
      auto *field = dynamic_cast<UIButtonTextField *>(Child(Root(), "btnInput"));
      if (!field) return false;
      const std::string commandText = field->inputText;
      std::string prompt = LegacyCaption("console_typehere");
      const std::size_t marker = prompt.find('>');
      if (marker != std::string::npos)
        prompt.resize(marker + 1);
      field->SetText("");
      if (!LegacySetCaption("console_typehere", prompt + commandText))
        return false;
      return LegacyClick("console_post");
    }
    if (command == "Back" || command == "Close" || command == "Done") return Back();
    if (command == "Commit") {
      if (active == SCREEN_RECORDSCREEN) {
        UIObject *list = Child(Root(), "recordFields");
        for (int i = 0;; ++i) {
          const std::string title = Indexed("recordscreen_title ", i);
          if (!LegacyHasButton(title.c_str())) break;
          UIObject *row = list ? list->FindChild("field:" + LegacyCaption(title.c_str())) : nullptr;
          if (row) LegacySetCaption(Indexed("recordscreen_value ", i).c_str(), GetText(Child(row, Indexed("txtValue", i))));
        }
        return LegacyClick("recordscreen_commit");
      }
      if (active == SCREEN_ACADEMICSCREEN && Remote()->security_level <= 2) {
        Sync("btnCollege", "academic_collegequals"); Sync("btnGrad", "academic_uniquals"); Sync("btnOther", "academic_otherquals");
        return LegacyClick("academic_commit");
      }
      if (active == SCREEN_SOCSECSCREEN && Remote()->security_level <= 2) {
        Sync("btnSSN", "ss_num"); Sync("btnDOB", "ss_dob"); Sync("btnMar", "ss_marital"); Sync("btnPer", "ss_personal");
        return LegacyClick("ss_commit");
      }
      return false;
    }
    if (active == SCREEN_RECORDSCREEN) {
      if (command == "Next") return LegacyClick("recordscreen_scrollright");
      if (command == "Prev") return LegacyClick("recordscreen_scrollleft");
    }
    if (active == SCREEN_LOANSSCREEN) {
      if (command == "LoanUp") return LegacyClick("loansscreen_increaseloan");
      if (command == "LoanDown") return LegacyClick("loansscreen_decreaseloan");
    }
    if (active == SCREEN_CRIMINALSCREEN && Remote()->security_level <= 2) {
      if (command == "Clear") return LegacyClick("criminal_clear");
      if (command == "Arrest") return LegacyClick("criminal_arrest");
      if (command == "AddConviction") {
        Sync("btnRecord", "criminal_newconvictiontext");
        const bool result = LegacyClick("criminal_newconvictionadd");
        Text(Root(), "btnRecord", LegacyCaption("criminal_newconvictiontext"));
        return result;
      }
    }
    if (active == SCREEN_SECURITYSCREEN && command.compare(0, 9, "Security:") == 0) {
      Computer *computer = ComputerModel();
      for (int i = 0; i < computer->security.NumSystems(); ++i)
        if (Identity(computer->security.GetSystem(i)) == command.substr(9))
          return LegacyClick(Indexed("securityscreen_systemstatus ", i).c_str());
      return false;
    }
    if (active == SCREEN_SWSALESSCREEN && command.compare(0, 17, "SoftwareCategory:") == 0) {
      const int category = std::atoi(command.c_str() + 17);
      if (category < 0 || category > 5) return false;
      swCategory = category; return true;
    }
    if (active == SCREEN_HWSALESSCREEN && command.compare(0, 17, "HardwareCategory:") == 0) {
      const int type = std::atoi(command.c_str() + 17);
      if (type != GATEWAYUPGRADETYPE_CPU && type != GATEWAYUPGRADETYPE_MEMORY && type != GATEWAYUPGRADETYPE_MODEM && type != GATEWAYUPGRADETYPE_SECURITY) return false;
      PrepareHardware(type); return true;
    }
    if ((active == SCREEN_SWSALESSCREEN || active == SCREEN_HWSALESSCREEN) && command.compare(0, 4, "Buy:") == 0) {
      Buy(command.substr(4)); return true;
    }
    if (active == SCREEN_SWSALESSCREEN && command.compare(0, 8, "Version:") == 0) {
      const std::string key = command.substr(8);
      Sale *sale = FindSale(key);
      if (!sale) return false;
      int &version = versions[key];
      if (!sale->GetVersion(++version)) version = 1;
      selected = key; return true;
    }
    if (active == SCREEN_CONTACTSCREEN && command.compare(0, 9, "Question:") == 0)
      return LegacyClick(("contact_question " + command.substr(9)).c_str());
    if (active == SCREEN_COMPANYINFO) {
      if (command == "AddManager" || command == "AddAdmin") {
        Company *company = game->GetWorld()->GetCompany(ComputerModel()->companyname);
        Person *person = company ? game->GetWorld()->GetPerson(command == "AddManager" ? company->boss : company->administrator) : nullptr;
        if (!person || !std::strcmp(person->phonenumber, " ")) return false;
        game->GetWorld()->GetPlayer()->GiveLink(person->phonenumber);
        return true;
      }
      if (command == "CallManager" || command == "CallAdmin") {
        Company *company = game->GetWorld()->GetCompany(ComputerModel()->companyname);
        Person *person = company ? game->GetWorld()->GetPerson(command == "CallManager" ? company->boss : company->administrator) : nullptr;
        if (!person || !game->GetWorld()->GetVLocation(person->phonenumber)) return false;
        Player *player = game->GetWorld()->GetPlayer();
        player->GiveLink(person->phonenumber);
        RemoteInterface *remote = Remote();
        Connection *connection = player->GetConnection();
        connection->Disconnect(); connection->Reset(); connection->AddVLocation(person->phonenumber); connection->Connect();
        remote->RunNewLocation(); return true;
      }
    }
    if (active == SCREEN_CHANGEGATEWAY && command.compare(0, 8, "Gateway:") == 0) {
      ChooseGateway(command.substr(8)); return true;
    }
    if (active == SCREEN_VOICEANALYSIS && command == "Access") return LegacyClick("voicescreen_access");
    if (active == SCREEN_VOICEPHONE && command == "RecordVoice") {
      auto *phone = dynamic_cast<VoicePhoneScreenInterface *>(Remote()->GetInterfaceScreen());
      if (!phone || phone->STATUS == VOICESTATUS_HUNGUP) return false;
      for (int i = 0; i < SvbNumTasks(); ++i) {
        TaskWrapper *wrapper = SvbGetTaskWrapperAtIndex(i);
        if (wrapper && wrapper->name && !std::strcmp(wrapper->name, "Voice_Analyser")) {
          SvbShowTask(wrapper->pid);
          game->GetInterface()->GetTaskManager()->SetTargetProgram(wrapper->pid);
          return true;
        }
      }
      if (LaunchOwnedApp("Voice_Analyser") < 0) {
        Info("Voice recording", "You require an executable Voice_Analyser to record this call."); return true;
      }
      return true;
    }
    if (active == SCREEN_PROTOVISION && command == "NuclearWar") {
      if (EclIsCaptionChangeActive("protovisionscreen_message") >= 0) return false;
      return LegacyClick((std::string("protovisionscreen_nuclearwar ") + ComputerModel()->ip).c_str());
    }
    if (active == SCREEN_NUCLEARWAR && command.compare(0, 5, "Nuke:") == 0)
      return LegacyClick(("nuclearwar_location " + command.substr(5)).c_str());
    if (active == SCREEN_RADIOTRANSMITTER) {
      if (command == "RadioDown") return LegacyClick("radioscreen_down");
      if (command == "RadioBigDown") return LegacyClick("radioscreen_bigdown");
      if (command == "RadioUp") return LegacyClick("radioscreen_up");
      if (command == "RadioBigUp") return LegacyClick("radioscreen_bigup");
      if (command == "Access") return LegacyClick("radioscreen_connect");
    }
    return false;
  }
};

HDRemoteServices::HDRemoteServices(UIObject &root) : state(new State(root)) {}
HDRemoteServices::~HDRemoteServices() = default;
void HDRemoteServices::Update() { state->Update(); }
void HDRemoteServices::Hide() { state->Hide(); }
bool HDRemoteServices::IsVisible() const { return state->View() && state->View()->IsVisible(); }
bool HDRemoteServices::Invoke(const std::string &command) { return state->Invoke(command); }
bool HDRemoteServices::HandleModalInput(const ALLEGRO_EVENT &event) {
  return state->popup.HandleInput(event);
}
bool HDRemoteServices::HandleInput(const ALLEGRO_EVENT &event) {
  if (HandleModalInput(event)) return true;
  if (!IsVisible() || !state->Current()) return false;
  UIObject *root = state->Root();
  const bool mouse = event.type == ALLEGRO_EVENT_MOUSE_AXES || event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN || event.type == ALLEGRO_EVENT_MOUSE_BUTTON_UP;
  if (mouse) {
    UIObject *capture = state->owner.GetPointerCapture();
    UIObject *workspace =
        root->FindChildRecursive("legacyWorkspaceBackdrop");
    if ((workspace && workspace->ContainsPoint(event.mouse.x, event.mouse.y)) ||
        (capture && capture->IsDescendantOf(root))) {
      if (event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN) {
        UIObject *hit =
            state->owner.HitTest(event.mouse.x, event.mouse.y);
        if (hit && hit->IsDescendantOf(root))
          state->View()->BringToFront();
      }
      state->owner.DispatchEvent(event);
      return true;
    }
    return false;
  }
  const bool keyboard = event.type == ALLEGRO_EVENT_KEY_DOWN ||
                        event.type == ALLEGRO_EVENT_KEY_UP ||
                        event.type == ALLEGRO_EVENT_KEY_CHAR;
  if (!keyboard) return false;
  UIObject *focus = state->owner.GetFocus();
  const bool ownsFocus = focus && focus->IsDescendantOf(root);
  if (focus && !ownsFocus) return false;
  if (event.type == ALLEGRO_EVENT_KEY_DOWN &&
      event.keyboard.keycode == ALLEGRO_KEY_ESCAPE) {
    state->Invoke("Back");
    return true;
  }
  if (!ownsFocus) return false;
  state->owner.DispatchEvent(event);
  return true;
}

} // namespace HDUI
