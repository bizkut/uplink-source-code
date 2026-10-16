#include "hd_remote_data.h"
#include "hd_legacy_bridge.h"
#include "hd_app_windows.h"
#include "app/app.h"
#include "app/globals.h"
#include "app/serialise.h"
#include "game/data/data.h"
#include "game/game.h"
#include "interface/interface.h"
#include "interface/localinterface/phonedialler.h"
#include "interface/remoteinterface/contactscreen_interface.h"
#include "interface/remoteinterface/remoteinterface.h"
#include "interface/remoteinterface/sharesviewscreen_interface.h"
#include "interface/taskmanager/decrypter.h"
#include "interface/taskmanager/filecopier.h"
#include "interface/taskmanager/filedeleter.h"
#include "interface/taskmanager/logdeleter.h"
#include "interface/taskmanager/logmodifier.h"
#include "interface/taskmanager/logundeleter.h"
#include "interface/taskmanager/taskmanager.h"
#include "world/company/companyuplink.h"
#include "world/company/mission.h"
#include "world/company/news.h"
#include "world/computer/computer.h"
#include "world/computer/computerscreen/bbsscreen.h"
#include "world/computer/computerscreen/genericscreen.h"
#include "world/computer/computerscreen/linksscreen.h"
#include "world/computer/computerscreen/logscreen.h"
#include "world/computer/computerscreen/shareslistscreen.h"
#include "world/computer/logbank.h"
#include "world/generator/missiongenerator.h"
#include "world/player.h"
#include "world/world.h"
#include "vanbakel.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <utility>
#include <vector>

namespace HDUI {
namespace {
struct ScreenSpec {
  int id;
  const char *layout;
  const char *list;
  const char *element;
};
const ScreenSpec screens[] = {
    {SCREEN_FILESERVERSCREEN, "FilesScreen", "filesList", "FileElement"},
    {SCREEN_LOGSCREEN, "LogsScreen", "logsList", "LogElement"},
    {SCREEN_LINKSSCREEN, "LinksScreen", "linksList", "LinkElement"},
    {SCREEN_BBSSCREEN, "BBSScreen", "missionsList", "BBSElement"},
    {SCREEN_NEWSSCREEN, "NewsScreen", "newsList", "NewsElement"},
    {SCREEN_SHARESLISTSCREEN, "SharesListScreen", "sharesList", "SharesListElement"},
    {SCREEN_SHARESVIEWSCREEN, "SharesViewScreen", nullptr, nullptr}};

UIObject *Child(UIObject *root, const char *name) {
  return root ? root->FindChildRecursive(name) : nullptr;
}
void Text(UIObject *root, const char *name, const std::string &value) {
  SetText(Child(root, name), value);
}
void Visible(UIObject *root, const char *name, bool value) {
  SetVisible(Child(root, name), value);
}
std::string Number(int number, const char *suffix = "") {
  return std::to_string(number) + suffix;
}
std::string PointerKey(const void *pointer) {
  char key[2 * sizeof(void *) + 8];
  std::snprintf(key, sizeof(key), "row_%p", pointer);
  return key;
}
std::string Lower(std::string value) {
  for (char &character : value)
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  return value;
}
bool Matches(const char *value, const std::string &filter) {
  return filter.empty() || Lower(value ? value : "").find(filter) != std::string::npos;
}
void Image(UIObject *root, const char *name, const char *file) {
  UIImage *image = dynamic_cast<UIImage *>(Child(root, name));
  if (!image || image->file == file)
    return;
  image->file = file;
  std::string directory = app->path;
  if (!directory.empty() && directory.back() != '/' && directory.back() != '\\')
    directory += '/';
  image->LoadBitmap(directory + "uplinkHD/graphics");
}
void Bind(UIObject *root, const char *name, ClickCallback callback) {
  UIObject *control = Child(root, name);
  if (control)
    control->onClickCallback = std::move(callback);
}
void HitArea(UIObject *root, const char *name, float x, float width,
             ClickCallback callback) {
  UIObject *control = root->FindChild(name);
  if (!control) {
    std::unique_ptr<UIInteractive> area(new UIInteractive());
    area->name = name;
    area->x = x;
    area->w = width;
    area->h = 44;
    area->focusable = true;
    control = area.get();
    root->AddChild(std::move(area));
  }
  control->onClickCallback = std::move(callback);
}

const char *MissionIcon(const Mission &mission, bool encrypted) {
  if (encrypted)
    return "icon_mission_unknown.png";
  switch (mission.TYPE) {
  case MISSION_STEALFILE:
    return mission.difficulty >= 5 ? "icon_mission_copyDatabase.png" : "icon_mission_copy.png";
  case MISSION_DESTROYFILE:
    return mission.difficulty >= 5 ? "icon_mission_destroyDatabase.png" : "icon_mission_delete.png";
  case MISSION_FINDDATA: return "icon_mission_financial.png";
  case MISSION_CHANGEDATA: return "icon_mission_social.png";
  case MISSION_FRAMEUSER: return "icon_mission_frame.png";
  case MISSION_TRACEUSER: return "icon_mission_traceHacker.png";
  case MISSION_CHANGEACCOUNT: return "icon_mission_transfer.png";
  case MISSION_REMOVECOMPUTER: return "icon_mission_destroyComputer.png";
  case MISSION_REMOVEUSER: return "icon_mission_ruinLife.png";
  case MISSION_SPECIAL:
    if (!std::strcmp(mission.description, "Counter attack!") ||
        !std::strcmp(mission.description, "Perform a covert installation of a Tracer") ||
        !std::strcmp(mission.description, "Help bring the CEO of a major company into custody") ||
        !std::strcmp(mission.description, "Take advantage of an amazing opportunity") ||
        !std::strcmp(mission.description, "Prevent the outbreak of Revelation"))
      return "icon_mission_arunmor.png";
    if (!std::strcmp(mission.description, "Test run Revelation Version 1.0") ||
        !std::strcmp(mission.description, "Steal vital Research into Digitial Life Forms") ||
        !std::strcmp(mission.description, "Frame the chief tech of Arunmor Corporation") ||
        !std::strcmp(mission.description, "Destroy all research completed by Arunmor") ||
        !std::strcmp(mission.description, "Release the Revelation Virus into the wild"))
      return "icon_mission_arc.png";
    break;
  }
  return "icon_mission_unknown.png";
}
const char *NewsIcon(const News &news) {
  const std::string headline(news.headline);
  static const char *arc[] = {
      "Unusual hiring practices noticed by analysts", "Top Uplink Agent re-assures community",
      "Federal Agents release findings on ARC", "ARC Internal Services System attacked",
      "CEO of Andromeda Research Corporation arrested", "ARC Central Mainframe compromised",
      "First press release from Andromeda Leader", "Leader of Andromeda jailed"};
  static const char *arunmor[] = {
      "Arunmor Security scare - a special report", "The Arunmor virus outbreak - a special report",
      "Arunmor: \"We'll retaliate\"", "Arunmor Corporation in Financial Fraud",
      "Arunmor LAN system suffers crippling blow"};
  static const char *uplink[] = {
      "High ranking Agent raises doubts over ARC", "Top agent working for ARC found dead",
      "Formal warning issued by Uplink Corporation"};
  static const char *revelation[] = {
      "Arunmor release details on Revelation", "Revelation Launch due soon?",
      "Revelation Virus Release stopped in its path", "Revelation virus outbreak leaves systems crippled",
      "Revelation : A retrospective", "Revelation outbreak yet again"};
  for (const char *title : arc)
    if (headline == title) return "iconNews_storyARC.png";
  for (const char *title : arunmor)
    if (headline == title) return "iconNews_storyArunmor.png";
  for (const char *title : uplink)
    if (headline == title) return "iconNews_storyUplink.png";
  for (const char *title : revelation)
    if (headline == title) return "iconNews_storyRevelation.png";
  if (headline.find("Unknown virus outbreak at") != std::string::npos ||
      headline.find("Revelation seen for second time at") != std::string::npos)
    return "iconNews_storyRevelation.png";
  if (headline == "New Uplink service available") return "iconNews_storyFaith.png";
  switch (news.NEWSTYPE) {
  case NEWS_TYPE_ARREST: return "iconNews_arrest.png";
  case NEWS_TYPE_DELETED: return "iconNews_deleted.png";
  case NEWS_TYPE_STOLEN: return "iconNews_stolen.png";
  case NEWS_TYPE_COMPUTERSHUTDOWN: return "iconNews_computerShutdown.png";
  case NEWS_TYPE_COMPUTERDESTROYED: return "iconNews_computerDestroyed.png";
  default: return "iconNews_hacked.png";
  }
}

struct Operation {
  int pid;
  const UplinkObject *source;
  int index;
  float progress;
};
template <class T>
void AddOperation(T *task, int pid, std::vector<Operation> &operations) {
  const int ticks = task->GetTicksRequired();
  operations.push_back({pid, task->GetSource(), task->GetSourceIndex(),
      ticks > 0 ? std::max(0.0f, std::min(1.0f,
          static_cast<float>(task->GetProgress()) / ticks)) : 0.0f});
}
void ReadOperations(std::vector<Operation> &operations) {
  // File/log targets are installed before the legacy Tick changes their idle
  // status. Keep those pending targets, and an open modifier editor, busy too.
  operations.clear();
  for (int index = 0; index < SvbNumTasks(); ++index) {
    TaskWrapper *wrapper = SvbGetTaskWrapperAtIndex(index);
    if (!wrapper || !wrapper->task)
      continue;
    Task *task = wrapper->task;
    if (auto *copy = dynamic_cast<FileCopier *>(task)) {
      if (copy->GetStatus() == FILECOPIER_INPROGRESS ||
          copy->GetStatus() == FILECOPIER_WAITINGFORTARGET ||
          (copy->GetStatus() == FILECOPIER_NOTDOWNLOADING && copy->GetSource()))
        AddOperation(copy, wrapper->pid, operations);
    } else if (auto *remove = dynamic_cast<FileDeleter *>(task)) {
      if (remove->GetStatus() == FILEDELETER_INPROGRESS ||
          (remove->GetStatus() == FILEDELETER_OFF && remove->GetSource()))
        AddOperation(remove, wrapper->pid, operations);
    } else if (auto *decrypt = dynamic_cast<Decrypter *>(task)) {
      if (decrypt->GetStatus() == DECRYPTER_INPROGRESS ||
          (decrypt->GetStatus() == DECRYPTER_OFF && decrypt->GetSource()))
        AddOperation(decrypt, wrapper->pid, operations);
    } else if (auto *removeLog = dynamic_cast<LogDeleter *>(task)) {
      if (removeLog->GetStatus() == LOGDELETER_INPROGRESS ||
          removeLog->GetStatus() == LOGDELETER_REPLACING ||
          (removeLog->GetStatus() == LOGDELETER_OFF && removeLog->GetSource()))
        AddOperation(removeLog, wrapper->pid, operations);
    } else if (auto *recoverLog = dynamic_cast<LogUnDeleter *>(task)) {
      if (recoverLog->GetStatus() == LOGUNDELETER_INPROGRESS ||
          (recoverLog->GetStatus() == LOGUNDELETER_OFF && recoverLog->GetSource()))
        AddOperation(recoverLog, wrapper->pid, operations);
    } else if (auto *modifyLog = dynamic_cast<LogModifier *>(task)) {
      if (modifyLog->GetStatus() == LOGMODIFIER_STATUS_ACTIVE ||
          modifyLog->GetStatus() == LOGMODIFIER_STATUS_MODIFYING ||
          modifyLog->GetStatus() == LOGMODIFIER_STATUS_CREATING)
        AddOperation(modifyLog, wrapper->pid, operations);
    }
  }
}
bool Progress(UIObject *row, const char *barName, const UplinkObject *bank,
              int memoryIndex, const std::vector<Operation> &operations) {
  float amount = -1;
  for (const Operation &operation : operations)
    if (operation.source == bank && operation.index == memoryIndex)
      amount = std::max(amount, operation.progress);
  UIObject *bar = Child(row, barName);
  SetVisible(bar, amount >= 0);
  Visible(row, "txtProgress", amount >= 0);
  if (amount >= 0) {
    if (bar) bar->scaleX = amount;
    Text(row, "txtProgress", Number(static_cast<int>(amount * 100), "%"));
  }
  return amount >= 0;
}
} // namespace

struct HDRemoteData::State {
  struct Row {
    UIObject *object;
    unsigned seen;
    std::function<void(int)> contextAction;
    bool busy = false;
  };
  struct View {
    std::unique_ptr<HDLayoutView> layout;
    std::unique_ptr<HDRepeatLayoutFactory> factory;
    std::map<std::string, Row> rows;
    ComputerScreen *model = nullptr;
    int page = -1;
    bool favourites = false;
    unsigned generation = 0;
  };

  explicit State(UIObject &owner) : ownerRoot(owner), popup(owner),
      context(owner, "Misc/ButtonsMenuReference"),
      contextFactory("Misc/ButtonsMenuReference"), active(-1),
      contextDismissKey(-1), selectedNews(nullptr), selectedMission(nullptr) {
    for (unsigned index = 0; index < views.size(); ++index) {
      views[index].layout.reset(new HDLayoutView(owner,
          std::string("HUD/interfaces/") + screens[index].layout));
      views[index].layout->SetCaptureInput(false);
      if (screens[index].element)
        views[index].factory.reset(new HDRepeatLayoutFactory(
            std::string("HUD/interfaces/Elements/") + screens[index].element));
    }
    context.SetCaptureInput(true);
  }

  UIObject &ownerRoot;
  std::array<View, 7> views;
  HDPopupController popup;
  HDLayoutView context;
  HDRepeatLayoutFactory contextFactory;
  std::vector<int> oneShots;
  std::string selectedRow;
  int active;
  int contextDismissKey;
  News *selectedNews;
  Mission *selectedMission;
  std::string selectedCompany;
  std::vector<Operation> operations;

  RemoteInterface *Remote() const {
    return game && game->GetInterface() ? game->GetInterface()->GetRemoteInterface() : nullptr;
  }
  ComputerScreen *Current() const {
    RemoteInterface *remote = Remote();
    return remote && remote->IsVisible() ? remote->GetComputerScreen() : nullptr;
  }
  bool Valid(ComputerScreen *model, int id) const {
    RemoteInterface *remote = Remote();
    return model && remote && active >= 0 && screens[active].id == id &&
        views[active].model == model && views[active].page == remote->currentscreenindex &&
        Current() == model && remote->GetInterfaceScreen()->ScreenID() == id;
  }
  CompanyUplink *Uplink() const {
    return static_cast<CompanyUplink *>(game->GetWorld()->GetCompany("Uplink"));
  }
  bool HasMission(Mission *mission) const {
    CompanyUplink *company = Uplink();
    for (int index = 0; index < company->missions.Size(); ++index)
      if (company->GetMission(index) == mission) return true;
    return false;
  }
  bool HasNews(News *news) const {
    CompanyUplink *company = Uplink();
    for (int index = 0; index < company->news.Size(); ++index)
      if (company->GetNews(index) == news) return true;
    return false;
  }
  UIObject *Root() const {
    return active >= 0 ? views[active].layout->Root() : nullptr;
  }
  void Hide() {
    popup.Hide();
    context.Hide();
    for (View &view : views) view.layout->Hide();
    active = -1;
    selectedRow.clear();
  }
  void CompleteOneShots() {
    if (oneShots.empty()) return;
    ReadOperations(operations);
    for (auto iterator = oneShots.begin(); iterator != oneShots.end();) {
      const int pid = *iterator;
      Task *task = SvbGetTask(pid);
      if (auto *copy = dynamic_cast<FileCopier *>(task)) {
        if (copy->GetStatus() == FILECOPIER_WAITINGFORTARGET) {
          DataBank &destination = game->GetWorld()->GetPlayer()->gateway.databank;
          char target[] = "memory";
          copy->SetTarget(&destination, target, -1);
          copy = dynamic_cast<FileCopier *>(SvbGetTask(pid));
          if (!copy) {
            iterator = oneShots.erase(iterator);
            continue;
          }
          if (copy->GetStatus() == FILECOPIER_WAITINGFORTARGET) {
            popup.ShowInfo("ERROR", "There is not enough free memory to store this file.");
            SvbRemoveTask(pid);
            iterator = oneShots.erase(iterator);
            continue;
          }
        }
      }
      bool pending = false;
      for (const Operation &operation : operations)
        if (operation.pid == pid) { pending = true; break; }
      if (!task || !pending) {
        if (task) SvbRemoveTask(pid);
        iterator = oneShots.erase(iterator);
      } else {
        ++iterator;
      }
    }
  }
  bool Busy(const UplinkObject *bank, int index) {
    ReadOperations(operations);
    for (const Operation &operation : operations)
      if (operation.source == bank && operation.index == index) return true;
    return false;
  }
  void StopOperation(const UplinkObject *bank, int index) {
    std::vector<Operation> running;
    ReadOperations(running);
    for (const Operation &operation : running)
      if (operation.source == bank && operation.index == index)
        SvbRemoveTask(operation.pid);
  }
  void FileAction(ComputerScreen *model, Data *file, int action) {
    if (action < 0 || action > 3 || !Valid(model, SCREEN_FILESERVERSCREEN)) return;
    DataBank &bank = model->GetComputer()->databank;
    const int memory = FileMemory(bank, file);
    if (memory < 0) return;
    if (action == 3) { StopOperation(&bank, memory); return; }
    if (Busy(&bank, memory)) return;
    if (model->GetComputer()->security.IsRunning_Firewall()) {
      popup.ShowInfo("ACCESS DENIED", "Denied access by Firewall");
      return;
    }
    if (action != 0 && Remote()->security_level > 2) {
      popup.ShowInfo("ACCESS DENIED", "Access Denied\nInsufficient Security Level");
      return;
    }
    if (action == 1 && file->encrypted == 0) {
      popup.ShowInfo("INFORMATION", "This file is not encrypted.");
      return;
    }
    if (action == 0 && game->GetWorld()->GetPlayer()->gateway.databank.FindValidPlacement(file) < 0) {
      popup.ShowInfo("ERROR", "There is not enough free memory to store this file.");
      return;
    }
    const char *software[] = {"File_Copier", "Decrypter", "File_Deleter"};
    CompleteOneShots();
    const int pid = action == 1 ? LaunchOwnedApp(software[action])
                                : LaunchBuiltinApp(software[action]);
    if (pid < 0) {
      popup.ShowInfo("WARNING", std::string(software[action]) +
          " is unavailable. Check your installed software and running instances.");
      return;
    }
    oneShots.push_back(pid);
    FileClick(model, file);
  }
  void LogAction(ComputerScreen *model, LogBank *bank, AccessLog *log, int hole, int action) {
    if (action < 0 || action > 3 || !Valid(model, SCREEN_LOGSCREEN) ||
        static_cast<LogScreen *>(model)->GetTargetLogBank() != bank) return;
    int index = hole;
    if (log) {
      index = -1;
      for (int candidate = 0; candidate < bank->logs.Size(); ++candidate)
        if (bank->logs.ValidIndex(candidate) && bank->logs.GetData(candidate) == log) {
          index = candidate;
          break;
        }
    }
    if (index < 0 || index >= bank->logs.Size() ||
        (!log && bank->logs.ValidIndex(index))) return;
    if (action == 3) { StopOperation(bank, index); return; }
    if (Busy(bank, index)) return;
    if ((action == 0 && (!log || !bank->internallogs.ValidIndex(index))) ||
        (action == 1 && !log)) return;
    if (model->GetComputer()->security.IsRunning_Proxy()) {
      popup.ShowInfo("ACCESS DENIED", "Denied access by Proxy Server");
      return;
    }
    const char *software[] = {"Log_UnDeleter", "Log_Modifier", "Log_Deleter"};
    CompleteOneShots();
    const int pid = LaunchOwnedApp(software[action]);
    if (pid < 0) {
      popup.ShowInfo("WARNING", std::string(software[action]) +
          " is unavailable. Check your installed software and running instances.");
      return;
    }
    if (action == 2 && !log) {
      auto *remove = dynamic_cast<LogDeleter *>(SvbGetTask(pid));
      if (!remove || remove->version != 4.0f) {
        SvbRemoveTask(pid);
        popup.ShowInfo("INFORMATION", "Removing an empty log slot requires Log_Deleter V4.0.");
        return;
      }
    }
    oneShots.push_back(pid);
    LogClick(model, bank, log, index);
  }
  void SelectRow(float pointerX, float pointerY) {
    if (active < 0 || !screens[active].list || ownerRoot.GetPointerCapture()) return;
    selectedRow.clear();
    UIObject *hit = ownerRoot.HitTest(pointerX, pointerY);
    View &view = views[active];
    const bool links = screens[active].id == SCREEN_LINKSSCREEN;
    for (const auto &entry : view.rows) {
      const Row &row = entry.second;
      if ((row.contextAction || links) && hit &&
          hit->IsDescendantOf(row.object)) {
        selectedRow = entry.first;
        return;
      }
    }
  }
  bool DispatchRowAction(const std::string &key, int action) {
    if (active < 0 || !Valid(views[active].model, screens[active].id) ||
        popup.IsVisible()) return false;
    View &view = views[active];
    auto found = view.rows.find(key);
    if (found == view.rows.end()) return false;
    Row &row = found->second;
    if (!row.contextAction || !row.object->IsVisible() || !row.object->IsEnabled() ||
        (row.busy && action != 3)) return false;
    context.Hide();
    row.contextAction(action);
    return true;
  }
  bool InvokeRowAction(const std::string &command) {
    const int id = screens[active].id;
    if (id == SCREEN_LINKSSCREEN) {
      const int action = command == "Bookmark" ? 1 :
                         command == "Favourite" ? 2 :
                         command == "Show On Map" ? 3 : -1;
      if (action < 0) return false;
      UIObject *focus = ownerRoot.GetFocus();
      if (selectedRow.empty() && focus)
        for (const auto &entry : views[active].rows)
          if (focus->IsDescendantOf(entry.second.object)) {
            selectedRow = entry.first;
            break;
          }
      auto found = views[active].rows.find(selectedRow);
      if (found == views[active].rows.end() ||
          !found->second.object->IsVisible() ||
          selectedRow.compare(0, 3, "ip_") != 0)
        return false;
      LinkAction(views[active].model, selectedRow.substr(3), action);
      return true;
    }
    int action = -1;
    if (id == SCREEN_FILESERVERSCREEN) {
      if (command == "Copy") action = 0;
      else if (command == "Decrypt") action = 1;
      else if (command == "Delete" || command == "Delete File") action = 2;
    } else if (id == SCREEN_LOGSCREEN) {
      if (command == "Undo") action = 0;
      else if (command == "Modify") action = 1;
      else if (command == "Delete" || command == "Delete Log") action = 2;
    }
    if (action < 0) return false;
    UIObject *focus = ownerRoot.GetFocus();
    if (focus && !focus->IsDescendantOf(Root()) &&
        !(context.IsVisible() && focus->IsDescendantOf(context.Root()))) return false;
    if (selectedRow.empty() && focus)
      for (const auto &entry : views[active].rows)
        if (entry.second.contextAction && focus->IsDescendantOf(entry.second.object)) {
          selectedRow = entry.first;
          break;
        }
    return DispatchRowAction(selectedRow, action);
  }
  bool OpenContext(float pointerX, float pointerY) {
    if (active < 0 || !screens[active].list ||
        !Valid(views[active].model, screens[active].id)) return false;
    View &view = views[active];
    UIObject *list = view.layout->Find(screens[active].list);
    if (!list || !list->IsVisible() || !list->ContainsPoint(pointerX, pointerY)) return false;
    UIObject *hit = ownerRoot.HitTest(pointerX, pointerY);
    for (const auto &entry : view.rows) {
      const Row &row = entry.second;
      if (!row.contextAction || !row.object->IsVisible() || !hit ||
          !hit->IsDescendantOf(row.object)) continue;
      selectedRow = entry.first;
      if (!context.Show()) return true;
      UIObject *menu = context.Root();
      const int actionCount = row.busy ? 4 : 3;
      const Geometry owner = menu->parent->GetGeometry();
      menu->x = std::max(0.0f, std::min(1590.0f, (pointerX - owner.x) / owner.sx));
      menu->y = std::max(0.0f, std::min(880.0f, (pointerY - owner.y) / owner.sy));
      menu->w = 330;
      menu->h = 18 + actionCount * 43;
      if (UIObject *old = menu->FindChild("actions")) menu->DetachChild(old);
      UIObject *base = menu->FindChild("baseGFX");
      if (base) { base->w = 330; base->h = menu->h - 8; }
      if (auto *top = dynamic_cast<UILine *>(menu->FindChild("anchorTop"))) {
        top->w = 330; top->x2 = 330;
      }
      if (auto *bottom = dynamic_cast<UILine *>(menu->FindChild("anchorBottom"))) {
        bottom->y = menu->h - 4; bottom->w = 330; bottom->x2 = 330;
      }
      for (const char *name : {"separatorRef", "btnBaseHoverRef", "btnBaseClickRef", "btnCaptionRef"})
        Visible(menu, name, false);
      std::unique_ptr<UIContainer> actions(new UIContainer());
      actions->name = "actions";
      const bool files = screens[active].id == SCREEN_FILESERVERSCREEN;
      const char *fileLabels[] = {"Copy", "Decrypt", "Remove", "Cancel (Esc)"};
      const char *logLabels[] = {"Undo", "Modify", "Remove", "Cancel (Esc)"};
      const std::string key = entry.first;
      for (int index = 0; index < actionCount; ++index) {
        std::unique_ptr<UIObject> style = contextFactory.Create();
        if (!style) continue;
        std::unique_ptr<UIButtonStandard> button(new UIButtonStandard());
        button->name = "action_" + Number(index);
        button->x = 7; button->y = 10 + index * 43; button->w = 316; button->h = 40;
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
            visual->x = 8; visual->y = 4;
            SetText(visual.get(), files ? fileLabels[index] : logLabels[index]);
          }
          button->AddChild(std::move(visual));
        }
        button->onClickCallback = [this, key, index](UIObject *) {
          DispatchRowAction(key, index);
        };
        actions->AddChild(std::move(button));
      }
      menu->AddChild(std::move(actions));
      menu->GetRoot()->SetFocus(Child(menu, "action_0"));
      return true;
    }
    return false;
  }
  UIObject *GetRow(View &view, UIScrollableList *list, const std::string &key, int order) {
    auto found = view.rows.find(key);
    if (found == view.rows.end()) {
      std::unique_ptr<UIObject> row = view.factory->Create();
      if (!row) return nullptr;
      row->name = key;
      row->zOrder = order;
      UIObject *object = row.get();
      list->AddChild(std::move(row));
      found = view.rows.emplace(key, Row{object, view.generation}).first;
    }
    Row &row = found->second;
    row.seen = view.generation;
    if (row.object->zOrder != order) row.object->SetZOrder(order);
    return row.object;
  }
  void FinishRows(View &view, UIScrollableList *list) {
    // This is called only by the manager's next Update, never by row callbacks.
    for (auto iterator = view.rows.begin(); iterator != view.rows.end();) {
      if (iterator->second.seen != view.generation) {
        if (iterator->first == selectedRow) {
          selectedRow.clear();
          context.Hide();
        }
        list->DetachChild(iterator->second.object);
        iterator = view.rows.erase(iterator);
      } else {
        ++iterator;
      }
    }
    list->ArrangeChildren();
    list->ClampScroll();
  }
  int FileMemory(DataBank &bank, Data *file) const {
    for (int index = 0; index < bank.GetDataSize(); ++index)
      if (bank.GetDataFile(index) == file) return bank.GetMemoryIndex(index);
    return -1;
  }
  void FileClick(ComputerScreen *model, Data *file) {
    if (!Valid(model, SCREEN_FILESERVERSCREEN)) return;
    Computer *computer = model->GetComputer();
    if (computer->security.IsRunning_Firewall()) {
      popup.ShowInfo("ACCESS DENIED", "Denied access by Firewall");
      return;
    }
    const int memory = file ? FileMemory(computer->databank, file) : -1;
    if (file && memory < 0) return;
    char action[128];
    if (file)
      std::snprintf(action, sizeof(action), "Accessed file %s", file->title);
    else
      std::snprintf(action, sizeof(action), "Accessed memory file index %d", memory);
    AccessLog *log = new AccessLog();
    char playerName[] = "PLAYER";
    log->SetProperties(&game->GetWorld()->date,
        game->GetWorld()->GetPlayer()->GetConnection()->GetGhost(), playerName,
        LOG_NOTSUSPICIOUS, LOG_TYPE_TEXT);
    log->SetData1(action);
    computer->logbank.AddLog(log);
    // Task SetTarget uses the Eclipse button for its position/security marker.
    // The memory index is resolved from the model pointer, never its display row.
    char target[] = "fileserverscreen_file 0";
    if (LegacyHasButton(target))
      game->GetInterface()->GetTaskManager()->SetProgramTarget(&computer->databank, target, memory);
  }
  void UpdateFiles(View &view, UIScrollableList *list) {
    Computer *computer = view.model->GetComputer();
    DataBank &bank = computer->databank;
    int order = 0, used = 0;
    for (int index = 0; index < bank.GetDataSize(); ++index) {
      Data *file = bank.GetDataFile(index);
      if (!file) continue;
      used += file->size;
      UIObject *row = GetRow(view, list, PointerKey(file), order++);
      if (!row) continue;
      Text(row, "txtName", file->title);
      Text(row, "txtSize", Number(file->size, " GQs"));
      Text(row, "txtEncr", file->encrypted > 0 ? "Level " + Number(file->encrypted) : "N/A");
      Text(row, "txtComp", file->compressed > 0 ? "Level " + Number(file->compressed) : "N/A");
      const int memory = bank.GetMemoryIndex(index);
      Row &record = view.rows.find(row->name)->second;
      record.busy = Progress(row, "dlProgress", &bank, memory, operations);
      if (record.contextAction) continue;
      ComputerScreen *model = view.model;
      Bind(row, "btnBase", [this, model, file](UIObject *) { FileClick(model, file); });
      record.contextAction = [this, model, file](int action) {
        FileAction(model, file, action);
      };
    }
    Visible(view.layout->Root(), "txtNoFiles", order == 0);
    Text(view.layout->Root(), "freeSpace/txtAmount", bank.GetSize() > used
        ? Number(bank.GetSize() - used) + "/" + Number(bank.GetSize(), " GQs") : "None");
    UIObject *fill = view.layout->Find("freeSpace/fillRect");
    if (fill) fill->scaleX = bank.GetSize() > 0
        ? std::max(0.0f, std::min(1.0f, static_cast<float>(used) / bank.GetSize())) : 0;
    UIObject *space = view.layout->Find("freeSpace");
    if (space) {
      space->interactive = true;
      ComputerScreen *model = view.model;
      space->onClickCallback = [this, model](UIObject *) { FileClick(model, nullptr); };
    }
  }
  void LogClick(ComputerScreen *model, LogBank *bank, AccessLog *log, int hole) {
    if (!Valid(model, SCREEN_LOGSCREEN) ||
        static_cast<LogScreen *>(model)->GetTargetLogBank() != bank) return;
    if (model->GetComputer()->security.IsRunning_Proxy()) {
      popup.ShowInfo("ACCESS DENIED", "Denied access by Proxy Server");
      return;
    }
    int index = hole;
    if (log) {
      index = -1;
      for (int candidate = 0; candidate < bank->logs.Size(); ++candidate)
        if (bank->logs.ValidIndex(candidate) && bank->logs.GetData(candidate) == log) {
          index = candidate;
          break;
        }
    } else if (index < 0 || index >= bank->logs.Size() || bank->logs.ValidIndex(index)) {
      return;
    }
    if (index < 0) return;
    char target[] = "logscreen_log 0";
    if (LegacyHasButton(target))
      game->GetInterface()->GetTaskManager()->SetProgramTarget(bank, target, index);
  }
  void UpdateLogs(View &view, UIScrollableList *list) {
    LogBank *bank = static_cast<LogScreen *>(view.model)->GetTargetLogBank();
    int first = 0, last = bank->logs.Size() - 1, order = 0;
    while (first <= last && !bank->logs.ValidIndex(first)) ++first;
    while (last >= first && !bank->logs.ValidIndex(last)) --last;
    for (int index = last; index >= first; --index) {
      AccessLog *log = bank->logs.ValidIndex(index) ? bank->logs.GetData(index) : nullptr;
      const std::string key = log ? PointerKey(log) : "erased_" + Number(index);
      UIObject *row = GetRow(view, list, key, order++);
      if (!row) continue;
      std::unique_ptr<char[]> description(log ? log->GetDescription() : nullptr);
      Text(row, "txtDate", log ? log->date.GetShortString() : "");
      Text(row, "txtDesc", description ? description.get() : "");
      Row &record = view.rows.find(row->name)->second;
      record.busy = Progress(row, "progressBar", bank, index, operations);
      if (record.contextAction) continue;
      ComputerScreen *model = view.model;
      Bind(row, "btnBase", [this, model, bank, log, index](UIObject *) {
        LogClick(model, bank, log, index);
      });
      record.contextAction = [this, model, bank, log, index](int action) {
        LogAction(model, bank, log, index, action);
      };
    }
    Visible(view.layout->Root(), "txtNoLogs", order == 0);
  }
  bool ProtectedLink(const std::string &ip) const {
    return ip == IP_INTERNIC || ip == IP_UPLINKINTERNALSERVICES || ip == IP_UPLINKCREDITSMACHINE;
  }
  void LinkAction(ComputerScreen *model, const std::string &ip, int action) {
    if (!Valid(model, SCREEN_LINKSSCREEN)) return;
    World *world = game->GetWorld();
    VLocation *location = world->GetVLocation(const_cast<char *>(ip.c_str()));
    if (!location) return;
    Player *player = world->GetPlayer();
    if (action == 0) {
      player->GetConnection()->Disconnect();
      player->GetConnection()->Reset();
      PhoneDialler *dialler = new PhoneDialler();
      dialler->DialNumber(100, 100, location->ip, 5);
    } else if (action == 1) {
      if (player->HasLink(location->ip)) {
        if (ProtectedLink(ip)) return;
        location->SetDisplayed(false);
        player->RemoveLink(location->ip);
      } else {
        player->GiveLink(location->ip);
      }
    } else if (action == 2) {
      location->SetColorDisplayed(!location->colored);
    } else {
      const bool displayed = !location->displayed;
      if (displayed && !player->HasLink(location->ip))
        player->GiveLink(location->ip);
      location->SetDisplayed(displayed);
    }
  }
  void UpdateLinks(View &view, UIScrollableList *list) {
    LinksScreen *model = static_cast<LinksScreen *>(view.model);
    Player *player = game->GetWorld()->GetPlayer();
    std::string filter = Lower(GetText(view.layout->Find("btnFilter")));
    std::vector<std::string> addresses;
    if (model->SCREENTYPE == LINKSSCREENTYPE_PLAYERLINKS) {
      for (int index = 0; index < player->links.Size(); ++index)
        addresses.emplace_back(player->links.GetData(index));
    } else {
      std::unique_ptr<DArray<VLocation *>> locations(game->GetWorld()->locations.ConvertToDArray());
      const std::string company = Lower(model->GetComputer()->companyname);
      for (int index = 0; index < locations->Size(); ++index) {
        if (!locations->ValidIndex(index)) continue;
        VLocation *location = locations->GetData(index);
        if ((model->SCREENTYPE == LINKSSCREENTYPE_ALLLINKS && location->listed) ||
            (model->SCREENTYPE == LINKSSCREENTYPE_LOCALLINKS &&
             location->GetOBJECTID() != OID_VLOCATIONSPECIAL && Matches(location->computer, company)))
          addresses.emplace_back(location->ip);
      }
    }
    int order = 0, visible = 0;
    for (const std::string &ip : addresses) {
      VLocation *location = game->GetWorld()->GetVLocation(const_cast<char *>(ip.c_str()));
      UIObject *row = GetRow(view, list, "ip_" + ip, order++);
      if (!row) continue;
      const bool show = Matches(location ? location->computer : "This link has expired", filter) &&
          (!view.favourites || (location && location->colored));
      row->visible = show;
      if (show) ++visible;
      Text(row, "txtName", location ? location->computer : "This link has expired");
      Text(row, "txtIP", ip);
      Visible(row, "iconBookmark", location && player->HasLink(location->ip));
      Visible(row, "iconFavOn", location && location->colored);
      Visible(row, "iconFavOff", !location || !location->colored);
      Visible(row, "iconMapOn", location && location->displayed);
      Visible(row, "iconMapOff", !location || !location->displayed);
      UIObject *button = Child(row, "btnBase");
      if (button && button->onClickCallback) continue;
      Bind(row, "btnBase", [this, model, ip](UIObject *) { LinkAction(model, ip, 0); });
      HitArea(row, "bookmarkAction", 0, 35,
          [this, model, ip](UIObject *) { LinkAction(model, ip, 1); });
      HitArea(row, "favouriteAction", 852, 30,
          [this, model, ip](UIObject *) { LinkAction(model, ip, 2); });
      HitArea(row, "mapAction", 882, 40,
          [this, model, ip](UIObject *) { LinkAction(model, ip, 3); });
    }
    Visible(view.layout->Root(), "txtNoLinks", visible == 0);
    Visible(view.layout->Root(), "btnFavs/iconOn", view.favourites);
    Visible(view.layout->Root(), "btnFavs/iconOff", !view.favourites);
  }
  void MissionAction(ComputerScreen *model, Mission *mission, int action) {
    if (!Valid(model, SCREEN_BBSSCREEN) || !HasMission(mission)) return;
    Player *player = game->GetWorld()->GetPlayer();
    if (!action) {
      if (player->rating.uplinkrating < mission->minuplinkrating) {
        popup.ShowInfo("ACCESS DENIED", "Encrypted (Insufficient Uplink Rating).");
        return;
      }
      selectedMission = selectedMission == mission ? nullptr : mission;
      return;
    }
    if (mission->TYPE == MISSION_TRACEUSER && !std::strcmp(mission->completionA, "PLAYER")) {
      popup.ShowInfo("ACCESS DENIED", action == 1
          ? "We cannot allow you to accept this mission, on the specific instructions of the employer."
          : "We cannot allow you to contact this employer, on their specific instructions.");
      return;
    }
    if (action == 1) {
      if (mission->acceptrating == -1) {
        popup.ShowInfo("ACCESS DENIED", "This mission cannot be auto-accepted.\nYou must contact the employers to accept this mission.");
      } else if (player->rating.uplinkrating < mission->acceptrating) {
        popup.ShowInfo("ACCESS DENIED", "You do not have a sufficient Uplink Rating to auto-accept this mission.\nYou must first contact the employers.");
      } else {
        CompanyUplink *company = Uplink();
        for (int index = 0; index < company->missions.Size(); ++index) {
          if (company->GetMission(index) != mission) continue;
          player->GiveMission(mission);
          company->missions.RemoveData(index);
          selectedMission = nullptr;
          break;
        }
      }
    } else {
      if (player->rating.uplinkrating < mission->minuplinkrating) {
        popup.ShowInfo("ACCESS DENIED", "You do not have a sufficient Uplink Rating to contact this employer.");
        return;
      }
      BBSScreen *bbs = static_cast<BBSScreen *>(model);
      if (bbs->contactpage == -1) return;
      Remote()->RunScreen(bbs->contactpage, bbs->GetComputer());
      auto *contact = dynamic_cast<ContactScreenInterface *>(Remote()->GetInterfaceScreen());
      if (contact) {
        contact->SetTYPE(CONTACT_TYPE_MISSION);
        contact->SetMission(mission);
      }
    }
  }
  void UpdateBBS(View &view, UIScrollableList *list) {
    CompanyUplink *company = Uplink();
    if (selectedMission && !HasMission(selectedMission)) selectedMission = nullptr;
    const int rating = game->GetWorld()->GetPlayer()->rating.uplinkrating;
    int order = 0;
    for (int index = 0; index < company->missions.Size(); ++index) {
      Mission *mission = company->GetMission(index);
      if (!mission) continue;
      UIObject *row = GetRow(view, list, PointerKey(mission), order++);
      if (!row) continue;
      const bool encrypted = rating < mission->minuplinkrating;
      row->alpha = encrypted ? 0.25f : 1.0f;
      Text(row, "txtDate", encrypted ? "Unknown" : mission->createdate.GetShortString());
      Text(row, "txtDesc", encrypted ? "Encrypted (Insufficient Uplink Rating)." : mission->description);
      Image(row, "icon", MissionIcon(*mission, encrypted));
      std::string details1 = "No additional information is available.", details2;
      const std::string details = mission->GetDetails() ? mission->GetDetails() : "";
      const size_t start = details.find("You will");
      const size_t end = start == std::string::npos ? start : details.find("space.", start);
      if (start != std::string::npos && end != std::string::npos)
        details1 = details.substr(start, end + 6 - start);
      if (details.find("The data is stored on a LAN.") != std::string::npos)
        details2 = "The data is stored on a LAN.";
      if (!details2.empty() && details1 == "No additional information is available.") {
        details1 = details2;
        details2.clear();
      }
      Text(row, "info/txtDetails1", details1);
      Text(row, "info/txtDetails2", details2);
      Text(row, "info/txtRankAmount", mission->acceptrating >= 0 && mission->acceptrating < NUM_UPLINKRATINGS
          ? Rating::GetUplinkRatingString(mission->acceptrating) : "Encrypted");
      Text(row, "info/txtPayAmount", Number(mission->payment, "c"));
      Text(row, "info/txtDiffAmount", Number(mission->difficulty));
      Visible(row, "info", selectedMission == mission && !encrypted);
      Visible(row, "separator", !encrypted);
      UIObject *button = Child(row, "btnBase");
      if (button && button->onClickCallback) continue;
      ComputerScreen *model = view.model;
      Bind(row, "btnBase", [this, model, mission](UIObject *) { MissionAction(model, mission, 0); });
      UIObject *info = Child(row, "info");
      if (info) {
        info->interactive = true;
        info->onClickCallback = [this, model, mission](UIObject *) { MissionAction(model, mission, 0); };
      }
      Bind(row, "info/btnAccept", [this, model, mission](UIObject *) { MissionAction(model, mission, 1); });
      Bind(row, "info/btnContact", [this, model, mission](UIObject *) { MissionAction(model, mission, 2); });
    }
  }
  void PrepareArticle(View &view) {
    // Move the authored paragraph into its authored scroll viewport; the XML's
    // sibling placement otherwise draws long stories outside the article mask.
    UIObject *opened = view.layout->Find("newsOpened");
    auto *scroll = dynamic_cast<UIScrollableList *>(Child(opened, "articleScroll"));
    if (!opened || !scroll || scroll->FindChild("txtDesc")) return;
    UIObject *description = opened->FindChild("txtDesc");
    if (!description) return;
    auto paragraph = opened->DetachChild(description);
    paragraph->x = 0;
    paragraph->y = 0;
    scroll->arrangeType = ArrangeType::ArrangeVertical;
    scroll->AddChild(std::move(paragraph));
  }
  void UpdateNews(View &view, UIScrollableList *list) {
    CompanyUplink *company = Uplink();
    if (selectedNews && !HasNews(selectedNews)) selectedNews = nullptr;
    int order = 0;
    for (int index = 0; index < company->news.Size(); ++index) {
      News *news = company->GetNews(index);
      if (!news) continue;
      UIObject *row = GetRow(view, list, PointerKey(news), order++);
      if (!row) continue;
      Text(row, "txtTitle", news->headline);
      Text(row, "txtDate", news->date.GetShortString());
      const std::string details = news->GetDetails() ? news->GetDetails() : "";
      Text(row, "txtDesc", details.substr(0, std::min(details.find('\n'), size_t(72))) +
          (details.size() > 72 ? "..." : ""));
      Image(row, "icon", NewsIcon(*news));
      UIObject *button = Child(row, "btnBase");
      if (button && button->onClickCallback) continue;
      ComputerScreen *model = view.model;
      Bind(row, "btnBase", [this, model, news](UIObject *) {
        if (!Valid(model, SCREEN_NEWSSCREEN) || !HasNews(news)) return;
        selectedNews = news;
        if (auto *scroll = dynamic_cast<UIScrollableList *>(Child(Root(), "articleScroll")))
          scroll->SetScroll(0, 0);
      });
    }
    PrepareArticle(view);
    Visible(view.layout->Root(), "newsOpened", selectedNews != nullptr);
    list->visible = selectedNews == nullptr;
    if (!selectedNews) return;
    UIObject *opened = view.layout->Find("newsOpened");
    Text(opened, "txtTitle", selectedNews->headline);
    Text(opened, "txtDate", std::string("Posted on ") + selectedNews->date.GetShortString() + " by Uplink");
    Text(opened, "txtDesc", selectedNews->GetDetails() ? selectedNews->GetDetails() : "");
    Image(opened, "icon", NewsIcon(*selectedNews));
    if (auto *paragraph = dynamic_cast<UITextParagraph *>(Child(opened, "txtDesc"))) {
      paragraph->PrepareText();
      paragraph->h = std::max(503.0f, static_cast<float>(paragraph->lines.size()) *
          (paragraph->fontSize + paragraph->leading + 2));
    }
  }
  void OpenCompany(ComputerScreen *model, const std::string &name) {
    if (!Valid(model, SCREEN_SHARESLISTSCREEN)) return;
    Company *company = game->GetWorld()->GetCompany(const_cast<char *>(name.c_str()));
    if (!company) return;
    SharesListScreen *shares = static_cast<SharesListScreen *>(model);
    if (shares->viewpage == -1) return;
    selectedCompany = name;
    Remote()->RunScreen(shares->viewpage, shares->GetComputer());
    auto *details = dynamic_cast<SharesViewScreenInterface *>(Remote()->GetInterfaceScreen());
    if (details) details->SetCompanyName(company->name);
  }
  void UpdateShares(View &view, UIScrollableList *list) {
    const std::string filter = Lower(GetText(view.layout->Find("btnFilter")));
    std::unique_ptr<DArray<Company *>> companies(game->GetWorld()->companies.ConvertToDArray());
    int order = 0;
    for (int index = 0; index < companies->Size(); ++index) {
      if (!companies->ValidIndex(index)) continue;
      Company *company = companies->GetData(index);
      if (company->TYPE != COMPANYTYPE_INDUSTRIAL && company->TYPE != COMPANYTYPE_COMMERCIAL &&
          company->TYPE != COMPANYTYPE_FINANCIAL) continue;
      UIObject *row = GetRow(view, list, "company_" + std::string(company->name), order++);
      if (!row) continue;
      row->visible = Matches(company->name, filter);
      Text(row, "txtName", company->name);
      Text(row, "txtPrice", Number(company->GetSharePrice(), " c"));
      const int change = company->GetShareChange();
      Text(row, "txtChange", (change > 0 ? "+ " : change < 0 ? "- " : "") + Number(std::abs(change), "%"));
      Visible(row, "iconUp", change >= 0);
      Visible(row, "iconDown", change < 0);
      Visible(row, "iconOwn", game->GetWorld()->GetPlayer()->NumSharesOwned(company->name) > 0);
      UIObject *button = Child(row, "btnBase");
      if (button && button->onClickCallback) continue;
      ComputerScreen *model = view.model;
      const std::string name(company->name);
      Bind(row, "btnBase", [this, model, name](UIObject *) { OpenCompany(model, name); });
    }
  }
  void UpdateShareView(View &view) {
    // The live Eclipse subtitle is also set for navigation originating outside
    // this controller; never infer the selected company from a display index.
    const std::string caption = LegacyCaption("sharesviewscreen_subtitle");
    selectedCompany = caption;
    Company *company = selectedCompany.empty() ? nullptr
        : game->GetWorld()->GetCompany(const_cast<char *>(selectedCompany.c_str()));
    UIObject *root = view.layout->Root();
    for (const auto &child : root->children)
      if (child->name != "txtTitle" && child->name != "txtSubtitle" && child->name != "btnBack")
        child->visible = company != nullptr;
    Text(root, "txtSubtitle", company ? company->name : "");
    if (!company) return;
    const int monthNow = company->GetShareLastMonthSet();
    const int yearNow = game->GetWorld()->date.GetYear();
    std::string months, prices;
    int maximum = 150;
    for (int month = 0; month < 12; ++month)
      maximum = std::max(maximum, company->GetSharePrice(month));
    for (int offset = 0; offset < 12; ++offset) {
      const int month = (monthNow - offset + 12) % 12;
      if (offset) { months += '\n'; prices += '\n'; }
      months += std::string(Date::GetMonthName(month + 1)) + ", " + Number(month > monthNow ? yearNow - 1 : yearNow);
      prices += Number(company->GetSharePrice(month), " c");
    }
    Text(root, "txtValMonths", months);
    Text(root, "txtValPrices", prices);
    Text(root, "txtValMin", "0");
    Text(root, "txtValMax", Number(maximum));
    // UIPoly closes paths. Twelve existing-style line widgets produce an open
    // time-series instead of the XML sample polygon's spurious closing edge.
    Visible(root, "graphPoly", false);
    UIObject *graph = Child(root, "liveShareGraph");
    if (!graph) {
      std::unique_ptr<UIContainer> container(new UIContainer());
      container->name = "liveShareGraph";
      container->x = 120; container->y = 360; container->w = 362; container->h = 362;
      graph = container.get();
      for (int segment = 0; segment < 11; ++segment) {
        std::unique_ptr<UILine> line(new UILine());
        line->name = "segment_" + Number(segment);
        line->strokeColor = "FFFFFF";
        line->strokeWidth = 2;
        container->AddChild(std::move(line));
      }
      root->AddChild(std::move(container));
    }
    for (int segment = 0; segment < 11; ++segment) {
      auto *line = static_cast<UILine *>(graph->children[segment].get());
      const int month1 = (monthNow - 11 + segment + 12) % 12;
      const int month2 = (month1 + 1) % 12;
      line->x1 = 1 + segment * (360.0f / 11);
      line->x2 = 1 + (segment + 1) * (360.0f / 11);
      line->y1 = 361 - company->GetSharePrice(month1) * (360.0f / maximum);
      line->y2 = 361 - company->GetSharePrice(month2) * (360.0f / maximum);
    }
    Player *player = game->GetWorld()->GetPlayer();
    const int owned = player->NumSharesOwned(company->name);
    const int paid = player->SharesPricePaid(company->name);
    const int value = owned * company->GetSharePrice();
    Text(root, "txtOwned", "You own " + Number(owned, " shares"));
    Text(root, "txtDiff", "Paid: " + Number(paid, "c") + " Value: " + Number(value, "c"));
    Text(root, "txtProfitAmount", Number(value - paid, "c"));
    const bool trade = Remote()->security_level < 10;
    for (const char *name : {"btnBuy", "btnSell", "txtOwned", "txtDiff", "txtProfit", "txtProfitAmount", "separator2"})
      Visible(root, name, trade);
    ComputerScreen *model = view.model;
    Bind(root, "btnBuy", [this, model](UIObject *) {
      if (Valid(model, SCREEN_SHARESVIEWSCREEN) && Remote()->security_level < 10)
        LegacyClick("sharesviewscreen_buy");
    });
    Bind(root, "btnSell", [this, model](UIObject *) {
      if (Valid(model, SCREEN_SHARESVIEWSCREEN) && Remote()->security_level < 10)
        LegacyClick("sharesviewscreen_sell");
    });
  }
  void Back(bool confirmed = false) {
    if (active < 0) return;
    ComputerScreen *model = views[active].model;
    const int id = screens[active].id;
    if (!views[active].layout->IsVisible() || !Valid(model, id)) { Hide(); return; }
    if (id == SCREEN_NEWSSCREEN && selectedNews) {
      selectedNews = nullptr;
      return;
    }
    const UplinkObject *bank = nullptr;
    if (id == SCREEN_FILESERVERSCREEN) bank = &model->GetComputer()->databank;
    if (id == SCREEN_LOGSCREEN) bank = static_cast<LogScreen *>(model)->GetTargetLogBank();
    std::vector<Operation> running;
    ReadOperations(running);
    bool accessing = false;
    for (const Operation &operation : running)
      if (operation.source == bank) accessing = true;
    if (bank && accessing && !confirmed) {
      popup.ShowConfirm("WARNING", id == SCREEN_FILESERVERSCREEN
          ? "You are currently accessing files. Going back will terminate these actions.\nAre you sure you want to continue?"
          : "You are currently accessing logs. Going back will terminate these actions.\nAre you sure you want to continue?",
          [this, model, id] { if (Valid(model, id)) Back(true); });
      return;
    }
    if (bank && confirmed)
      for (const Operation &operation : running)
        if (operation.source == bank) SvbRemoveTask(operation.pid);
    Remote()->GetInterfaceScreen()->EscapeKeyPressed();
    if (!Valid(model, id)) Hide();
  }
  void Update() {
    CompleteOneShots();
    ComputerScreen *model = Current();
    int next = -1;
    if (model) {
      const int id = Remote()->GetInterfaceScreen()->ScreenID();
      for (unsigned index = 0; index < views.size(); ++index)
        if (screens[index].id == id) next = static_cast<int>(index);
    }
    if (next < 0) { Hide(); return; }
    if (active != next) {
      if (active >= 0) views[active].layout->Hide();
      popup.Hide();
      context.Hide();
      active = next;
      selectedRow.clear();
    }
    View &view = views[active];
    if (!view.layout->Root()) view.rows.clear();
    const bool showing = view.layout->IsVisible();
    if (!showing && !view.layout->Show()) return;
    if (view.model != model || view.page != Remote()->currentscreenindex) {
      context.Hide();
      popup.Hide();
      selectedRow.clear();
      if (auto *list = dynamic_cast<UIScrollableList *>(screens[active].list
              ? view.layout->Find(screens[active].list) : nullptr)) {
        list->ClearChildren();
        list->SetScroll(0, 0);
      }
      view.rows.clear();
      view.model = model;
      view.page = Remote()->currentscreenindex;
      if (screens[active].id == SCREEN_NEWSSCREEN) selectedNews = nullptr;
    }
    UIObject *root = view.layout->Root();
    root->interactive = false;
    root->onClickCallback = nullptr;
    Text(root, "txtTitle", model->maintitle);
    Text(root, "txtSubtitle", model->subtitle);
    Bind(root, "btnBack", [this](UIObject *) { Back(); });
    if (screens[active].id == SCREEN_LINKSSCREEN) {
      Bind(root, "btnFavs", [&view](UIObject *) { view.favourites = !view.favourites; });
    }
    if (screens[active].id == SCREEN_BBSSCREEN) {
      Text(root, "txtTitle", "Uplink");
      Text(root, "txtSubtitle", "Jobs BBS");
    }
    if (screens[active].id == SCREEN_NEWSSCREEN) {
      Text(root, "txtTitle", "Uplink");
      Text(root, "txtSubtitle", "Latest News");
    }
    ReadOperations(operations);
    ++view.generation;
    auto *list = dynamic_cast<UIScrollableList *>(screens[active].list
        ? view.layout->Find(screens[active].list) : nullptr);
    switch (screens[active].id) {
    case SCREEN_FILESERVERSCREEN: if (list) UpdateFiles(view, list); break;
    case SCREEN_LOGSCREEN: if (list) UpdateLogs(view, list); break;
    case SCREEN_LINKSSCREEN: if (list) UpdateLinks(view, list); break;
    case SCREEN_BBSSCREEN: if (list) UpdateBBS(view, list); break;
    case SCREEN_NEWSSCREEN: if (list) UpdateNews(view, list); break;
    case SCREEN_SHARESLISTSCREEN: if (list) UpdateShares(view, list); break;
    case SCREEN_SHARESVIEWSCREEN: UpdateShareView(view); break;
    }
    if (list) FinishRows(view, list);
    if (context.IsVisible()) context.BringToFront();
    if (popup.IsVisible()) popup.Focus();
  }
};

HDRemoteData::HDRemoteData(UIObject &ownerRoot) : state(new State(ownerRoot)) {}
HDRemoteData::~HDRemoteData() = default;
void HDRemoteData::Update() { state->Update(); }
void HDRemoteData::Hide() { state->Hide(); }
bool HDRemoteData::IsVisible() const {
  return state->active >= 0 && state->views[state->active].layout->IsVisible() &&
      state->Valid(state->views[state->active].model, screens[state->active].id);
}
bool HDRemoteData::Invoke(const std::string &command) {
  if (!IsVisible()) return false;
  if (command == "remote.back" || command == "back") {
    state->Back();
    return true;
  }
  if (command == "remote.links.favourites" && screens[state->active].id == SCREEN_LINKSSCREEN) {
    State::View &view = state->views[state->active];
    view.favourites = !view.favourites;
    return true;
  }
  return state->InvokeRowAction(command);
}
bool HDRemoteData::HandleModalInput(const ALLEGRO_EVENT &event) {
  if (state->active >= 0 &&
      !state->Valid(state->views[state->active].model, screens[state->active].id))
    state->Hide();
  const bool keyboard = event.type == ALLEGRO_EVENT_KEY_DOWN ||
                        event.type == ALLEGRO_EVENT_KEY_CHAR ||
                        event.type == ALLEGRO_EVENT_KEY_UP;
  if (keyboard && state->contextDismissKey == event.keyboard.keycode) {
    if (event.type == ALLEGRO_EVENT_KEY_UP)
      state->contextDismissKey = -1;
    return true;
  }
  if (state->popup.HandleInput(event)) return true;
  if (!state->context.IsVisible()) return false;
  if (keyboard) {
    if (event.type == ALLEGRO_EVENT_KEY_DOWN) {
      const int key = event.keyboard.keycode;
      UIObject *actions = state->context.Root()->FindChild("actions");
      if (key == ALLEGRO_KEY_ESCAPE) {
        state->contextDismissKey = key;
        state->context.Hide();
      } else if (actions && !actions->children.empty() &&
                 (key == ALLEGRO_KEY_TAB || key == ALLEGRO_KEY_UP ||
                  key == ALLEGRO_KEY_DOWN)) {
        UIObject *focused = state->ownerRoot.GetFocus();
        std::size_t index = 0;
        for (std::size_t i = 0; i < actions->children.size(); ++i)
          if (actions->children[i].get() == focused) index = i;
        const bool previous = key == ALLEGRO_KEY_UP ||
            (key == ALLEGRO_KEY_TAB &&
             (event.keyboard.modifiers & ALLEGRO_KEYMOD_SHIFT));
        for (std::size_t count = 0; count < actions->children.size(); ++count) {
          index = previous
              ? (index + actions->children.size() - 1) % actions->children.size()
              : (index + 1) % actions->children.size();
          if (actions->children[index]->IsEnabled()) break;
        }
        state->ownerRoot.SetFocus(actions->children[index].get());
        state->contextDismissKey = key;
      } else if (actions && (key == ALLEGRO_KEY_ENTER ||
                             key == ALLEGRO_KEY_PAD_ENTER ||
                             key == ALLEGRO_KEY_SPACE)) {
        UIObject *focused = state->ownerRoot.GetFocus();
        if (focused && focused->IsDescendantOf(actions))
          focused->OnClick();
        state->contextDismissKey = key;
      }
    }
    return true;
  }
  if (event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN &&
      !state->context.Root()->ContainsPoint(event.mouse.x, event.mouse.y)) {
    state->context.Hide();
    return true;
  }
  state->context.Root()->GetRoot()->DispatchEvent(event);
  return true;
}
bool HDRemoteData::HandleInput(const ALLEGRO_EVENT &event) {
  if (HandleModalInput(event)) return true;
  if (!IsVisible()) return false;
  UIObject *root = state->Root();
  if (event.type == ALLEGRO_EVENT_MOUSE_AXES ||
      event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN)
    state->SelectRow(event.mouse.x, event.mouse.y);
  if (event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN) {
    UIObject *hit = state->ownerRoot.HitTest(event.mouse.x, event.mouse.y);
    if (hit && hit->IsDescendantOf(root))
      state->views[state->active].layout->BringToFront();
  }
  if (event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN && event.mouse.button == 2 &&
      state->OpenContext(event.mouse.x, event.mouse.y))
    return true;
  if (event.type == ALLEGRO_EVENT_KEY_DOWN &&
      event.keyboard.keycode == ALLEGRO_KEY_ESCAPE) {
    UIObject *focus = state->ownerRoot.GetFocus();
    if (focus && !focus->IsDescendantOf(root))
      return false;
    state->Back();
    return true;
  }
  const bool pointer = event.type == ALLEGRO_EVENT_MOUSE_AXES ||
                       event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN ||
                       event.type == ALLEGRO_EVENT_MOUSE_BUTTON_UP;
  UIObject *workspace = Child(root, "legacyWorkspaceBackdrop");
  UIObject *capture = state->ownerRoot.GetPointerCapture();
  if (pointer &&
      ((workspace && workspace->ContainsPoint(event.mouse.x, event.mouse.y)) ||
       (capture && capture->IsDescendantOf(root)))) {
    state->ownerRoot.DispatchEvent(event);
    return true;
  }
  return false;
}
} // namespace HDUI
