#include "hd_remote_core.h"
#include "hd_legacy_bridge.h"
#include "app/globals.h"
#include "app/serialise.h"
#include "redshirt.h"
#include "soundgarden.h"
#include "game/game.h"
#include "game/data/data.h"
#include "game/scriptlibrary.h"
#include "app/opengl_interface.h"
#include "world/generator/numbergenerator.h"
#include "interface/interface.h"
#include "interface/remoteinterface/remoteinterface.h"
#include "interface/remoteinterface/cypherscreen_interface.h"
#include "world/world.h"
#include "world/player.h"
#include "world/computer/computer.h"
#include "world/computer/computerscreen/dialogscreen.h"
#include "world/computer/computerscreen/disconnectedscreen.h"
#include "world/computer/computerscreen/genericscreen.h"
#include "world/computer/computerscreen/highsecurityscreen.h"
#include "world/computer/computerscreen/menuscreen.h"
#include "world/computer/computerscreen/messagescreen.h"
#include "world/computer/computerscreen/passwordscreen.h"
#include "world/computer/computerscreen/useridscreen.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace HDUI {
namespace {
const char *Text(const char *value) { return value ? value : ""; }

RemoteInterface *CurrentRemote() {
  if (!game || game->GameSpeed() < GAMESPEED_PAUSED ||
      !LegacyHasButton("hud_mainmenu"))
    return nullptr;
  RemoteInterface *remote = game->GetInterface()->GetRemoteInterface();
  return remote && remote->IsVisible() ? remote : nullptr;
}

const char *LayoutFor(int type) {
  switch (type) {
  case SCREEN_MESSAGESCREEN: return "MessageScreen";
  case SCREEN_MENUSCREEN: return "MenuScreen";
  case SCREEN_DIALOGSCREEN: return "DialogScreen";
  case SCREEN_PASSWORDSCREEN: return "PasswordScreen";
  case SCREEN_USERIDSCREEN: return "UserIDScreen";
  case SCREEN_DISCONNECTEDSCREEN: return "DisconnectedScreen";
  case SCREEN_HIGHSECURITYSCREEN: return "HighSecurityScreen";
  case SCREEN_CYPHERSCREEN: return "CypherScreen";
  default: return nullptr;
  }
}

bool Editable(int type) {
  return type == WIDGET_TEXTBOX || type == WIDGET_PASSWORDBOX ||
         type == WIDGET_FIELDVALUE;
}
bool Action(int type) {
  return type == WIDGET_NEXTPAGE || type == WIDGET_SCRIPTBUTTON ||
         type == WIDGET_IMAGEBUTTON;
}
std::string WidgetButton(DialogScreenWidget &widget, Computer &computer) {
  std::ostringstream name;
  name << Text(widget.GetName()) << ' ' << widget.data1 << ' ' << widget.data2;
  if (Action(widget.WIDGETTYPE))
    name << ' ' << computer.ip;
  return name.str();
}

void DialogActionClick(Button *button) {
  RemoteInterface *remote = CurrentRemote();
  if (!button || !remote || remote->GetInterfaceScreen()->ScreenID() != SCREEN_DIALOGSCREEN)
    return;
  DialogScreen *dialog = static_cast<DialogScreen *>(remote->GetComputerScreen());
  Computer *computer = dialog->GetComputer();
  for (int index = 0; index < dialog->widgets.Size(); ++index) {
    DialogScreenWidget *widget = dialog->widgets.GetData(index);
    if (!widget || !Action(widget->WIDGETTYPE) || WidgetButton(*widget, *computer) != button->name)
      continue;
    const bool script = widget->WIDGETTYPE == WIDGET_SCRIPTBUTTON;
    const int scriptIndex = widget->data1;
    const int next = script ? widget->data2 : widget->data1;
    // The script may remove its own widget or replace the complete screen.
    if (script && scriptIndex != -1)
      ScriptLibrary::RunScript(scriptIndex);
    if (next != -1)
      remote->RunScreen(next, computer);
    return;
  }
}

void EnsureWidgetBackend(DialogScreenWidget &widget, Computer &computer,
                         const std::string &name, const char *securityName) {
  if (LegacyHasButton(name.c_str()))
    return;
  // AddWidget can append to an already-visible dialog without asking its
  // Eclipse interface to Create again. Register the real widget's script input,
  // not a dummy control, so the unchanged scripts see its exact editable ID.
  char *id = const_cast<char *>(name.c_str());
  EclRegisterButton(widget.x, widget.y, widget.width, widget.height,
                    widget.GetCaption(), widget.GetTooltip(), id);
  EclRegisterButtonCallbacks(id, nullptr, Action(widget.WIDGETTYPE) ? DialogActionClick : nullptr,
                            nullptr, nullptr);
  if (Editable(widget.WIDGETTYPE)) {
    EclMakeButtonEditable(id);
    if (widget.WIDGETTYPE == WIDGET_FIELDVALUE) {
      Record *record = computer.recordbank.GetRecordFromName(const_cast<char *>(securityName));
      if (record) {
        const char *value = record->GetField(widget.GetStringData1());
        if (value)
          LegacySetCaption(id, value);
      }
    }
  }
  if (widget.WIDGETTYPE == WIDGET_IMAGE || widget.WIDGETTYPE == WIDGET_IMAGEBUTTON)
    button_assignbitmaps(id, widget.GetStringData1(), widget.GetStringData2(), widget.GetStringData2());
}

void CollectCodes(BTree<char *> *tree, const char *ip,
                  std::vector<std::string> &codes) {
  if (!tree || !tree->id)
    return;
  const int comparison = std::strcmp(ip, tree->id);
  if (comparison <= 0)
    CollectCodes(tree->Left(), ip, codes);
  if (comparison == 0 && tree->data)
    codes.emplace_back(tree->data);
  if (comparison > 0)
    CollectCodes(tree->Right(), ip, codes);
}

std::vector<std::string> Quoted(const std::string &text) {
  std::vector<std::string> result;
  std::size_t start = 0;
  while ((start = text.find('\'', start)) != std::string::npos) {
    const std::size_t end = text.find('\'', start + 1);
    if (end == std::string::npos)
      break;
    result.push_back(text.substr(start + 1, end - start - 1));
    start = end + 1;
  }
  return result;
}

// Arbitrary dialog images retain their real Eclipse-loaded assets, including
// archived TIFs. A bitmap is uploaded once per image state, never every frame.
class DialogImage : public UIObject {
public:
  explicit DialogImage(const std::string &name) : buttonName(name) {}
  ~DialogImage() override {
    for (auto &entry : images)
      al_destroy_bitmap(entry.second);
  }
  void Draw() override {
    if (!IsVisible())
      return;
    Button *button = LegacyFindButton(buttonName.c_str());
    if (!button)
      return;
    Image *image = button->image_standard;
    if (parent && parent->isPressed && button->image_clicked)
      image = button->image_clicked;
    else if (parent && parent->isHovered && button->image_highlighted)
      image = button->image_highlighted;
    if (!image || !image->pixels || image->Width() <= 0 || image->Height() <= 0)
      return;
    auto found = images.find(image);
    if (found == images.end()) {
      const int flags = al_get_new_bitmap_flags();
      al_set_new_bitmap_flags(ALLEGRO_MEMORY_BITMAP);
      ALLEGRO_BITMAP *bitmap = al_create_bitmap(image->Width(), image->Height());
      al_set_new_bitmap_flags(flags);
      if (!bitmap)
        return;
      ALLEGRO_LOCKED_REGION *lock = al_lock_bitmap(bitmap, ALLEGRO_PIXEL_FORMAT_ABGR_8888_LE,
                                                  ALLEGRO_LOCK_WRITEONLY);
      if (!lock) {
        al_destroy_bitmap(bitmap);
        return;
      }
      for (int row = 0; row < image->Height(); ++row)
        std::memcpy(static_cast<unsigned char *>(lock->data) + row * lock->pitch,
                    image->pixels + row * image->Width() * 4, image->Width() * 4);
      al_unlock_bitmap(bitmap);
      found = images.emplace(image, bitmap).first;
    }
    const Geometry geometry = GetGeometry();
    al_draw_tinted_scaled_bitmap(found->second,
        al_map_rgba_f(1, 1, 1, geometry.alpha * image->GetAlpha()), 0, 0,
        image->Width(), image->Height(), geometry.x, geometry.y, geometry.w,
        geometry.h, 0);
  }
private:
  std::string buttonName;
  std::map<Image *, ALLEGRO_BITMAP *> images;
};

void HideControls(UIObject *object) {
  if (!object)
    return;
  // Only authored dialog controls are hidden; decorative separators/icons stay.
  for (auto &child : object->children) {
    if (dynamic_cast<UIButtonStandard *>(child.get()) ||
        dynamic_cast<UITextPoint *>(child.get()))
      SetVisible(child.get(), false);
  }
}
} // namespace

struct HDRemoteCore::State {
  struct Field {
    UIButtonTextField *control;
    std::string legacyName, synchronized;
  };
  struct Widget {
    UIObject *control;
    int type;
    std::string legacyName;
    bool generated;
  };
  explicit State(UIObject &root)
      : owner(root), model(nullptr), legacy(nullptr), type(0), page(-1),
        generic(nullptr), selectedDialog(nullptr), codesList(nullptr), menuList(nullptr),
        cypherTick(0), cypherLocks(-1) {}

  UIObject &owner;
  std::unique_ptr<HDLayoutView> view;
  ComputerScreen *model;
  RemoteInterfaceScreen *legacy;
  int type, page;
  UIObject *generic, *selectedDialog;
  UIScrollableList *codesList, *menuList;
  std::string dialogName, lostLogins;
  std::map<std::string, Field> fields;
  std::map<DialogScreenWidget *, Widget> widgets;
  std::map<std::string, UIObject *> codeRows, menuRows;
  unsigned cypherTick;
  int cypherLocks;
  std::array<char, CYPHER_WIDTH> cypherDigits;

  UIObject *Find(const std::string &name) const {
    return view ? view->Find(name, false) : nullptr;
  }
  bool Matches() const {
    RemoteInterface *remote = CurrentRemote();
    return remote && remote->GetComputerScreen() == model &&
           remote->GetInterfaceScreen() == legacy && remote->currentscreenindex == page;
  }
  void Bind(UIObject *control, const std::string &command) {
    if (!control)
      return;
    control->onClickCallback = [this, command](UIObject *) {
      if (!Matches())
        return;
      view->BringToFront();
      Invoke(command);
      // Do not destroy a control from inside its own callback. Hide retains
      // the complete old view until the next Update outside input dispatch.
      if (!Matches() && view)
        view->Hide();
    };
  }
  void Bind(const char *name, const char *command) { Bind(Find(name), command); }
  void AddField(UIObject *control, const std::string &name, bool password) {
    auto *field = dynamic_cast<UIButtonTextField *>(control);
    if (!field)
      return;
    field->isPassword = password;
    field->maxLength = -1;
    field->multiline = !password && field->h > 60;
    if (field->multiline) {
      auto *caption = dynamic_cast<UITextPoint *>(field->FindChild("txtCaption"));
      if (caption && !dynamic_cast<UITextParagraph *>(caption)) {
        std::unique_ptr<UITextParagraph> paragraph(new UITextParagraph());
        paragraph->name = caption->name;
        paragraph->x = caption->x; paragraph->y = caption->y;
        paragraph->w = std::max(1.0f, field->w - paragraph->x * 2);
        paragraph->h = std::max(1.0f, field->h - paragraph->y * 2);
        paragraph->font = caption->font;
        paragraph->assetBasePath = caption->assetBasePath;
        paragraph->fontSize = caption->fontSize;
        paragraph->fillColor = caption->fillColor;
        paragraph->text = caption->text;
        field->DetachChild(caption);
        field->AddChild(std::move(paragraph));
      }
    }
    field->onClickCallback = [this](UIObject *) { if (view) view->BringToFront(); };
    const std::string value = LegacyCaption(name.c_str());
    field->SetText(value);
    fields[name] = {field, name, value};
    field->onEnterCallback = [this](UIObject *) { Invoke("return"); };
  }
  void SynchronizeFields() {
    for (auto &entry : fields) {
      Field &field = entry.second;
      if (!LegacyHasButton(field.legacyName.c_str()))
        continue;
      if (field.control->inputText != field.synchronized) {
        if (SyncLegacyInput(field.control, field.legacyName.c_str()))
          field.synchronized = field.control->inputText;
      } else {
        const std::string value = LegacyCaption(field.legacyName.c_str());
        if (value != field.synchronized) {
          field.control->SetText(value);
          field.synchronized = value;
        }
      }
    }
  }
  void Hide() {
    if (view)
      view->Hide();
  }

  std::string SelectDialog(DialogScreen &dialog) {
    // These are semantic identities in the live widget model, not translated
    // screen titles. Mod-created dialogs with other widgets use the same HD
    // fallback and the same Eclipse callbacks.
    for (int index = 0; index < dialog.widgets.Size(); ++index) {
      DialogScreenWidget *widget = dialog.widgets.GetData(index);
      if (!widget || widget->WIDGETTYPE != WIDGET_SCRIPTBUTTON)
        continue;
      switch (widget->data1) {
      case 33: return "uplinkScreen3";
      case 43: return "gatewayScreen7";
      case 11: return "bankScreen4";
      case 12: return "stockScreen2";
      case 10: return "bankScreen7";
      case 15: return "criminalScreen7";
      case 16: return "academicScreen5";
      case 17: return "socialScreen5";
      case 60: case 61: case 62: case 63: return "deadAgentEmail3";
      case 80: case 81: case 82: case 83: return "lanScreen";
      default: break;
      }
    }
    Computer *computer = dialog.GetComputer();
    if (std::strcmp(computer->ip, IP_LOCALHOST) == 0) {
      if (page == 3 || page == 6 || page == 7)
        return "gatewayScreen" + std::to_string(page);
    }
    for (int index = 0; index < dialog.widgets.Size(); ++index) {
      DialogScreenWidget *widget = dialog.widgets.GetData(index);
      if (!widget)
        continue;
      const std::string name = Text(widget->GetName());
      if (name == "okbutton" && widget->data1 == 7)
        return "gatewayScreen6";
      if (name == "okbutton" && widget->data1 == 0)
        return "gatewayScreen3";
      if (name == "close" && (computer->TYPE & COMPUTER_TYPE_PUBLICBANKSERVER))
        return "bankScreen19";
      if (name == "close" && std::string(dialog.subtitle) == "eMailz")
        return "deadAgentEmail3";
    }
    if (std::strcmp(computer->ip, IP_UPLINKPUBLICACCESSSERVER) == 0 &&
        (page == 2 || page == 5 || page == 10))
      return "uplinkScreen" + std::to_string(page);
    return std::string();
  }

  std::unique_ptr<UIObject> MakeWidget(DialogScreenWidget &widget,
                                      const std::string &legacyName) {
    std::unique_ptr<UIObject> result;
    if (Editable(widget.WIDGETTYPE)) {
      HDRepeatLayoutFactory factory("HUD/interfaces/PasswordScreen", "btnPassword");
      result = factory.Create();
    } else if (Action(widget.WIDGETTYPE) && widget.WIDGETTYPE != WIDGET_IMAGEBUTTON) {
      HDRepeatLayoutFactory factory("HUD/interfaces/MessageScreen", "btnOK");
      result = factory.Create();
    } else if (widget.WIDGETTYPE == WIDGET_IMAGE || widget.WIDGETTYPE == WIDGET_IMAGEBUTTON) {
      if (widget.WIDGETTYPE == WIDGET_IMAGEBUTTON) {
        std::unique_ptr<UIButtonStandard> button(new UIButtonStandard());
        std::unique_ptr<DialogImage> image(new DialogImage(legacyName));
        image->w = std::max(1, widget.width) * 1.9f;
        image->h = std::max(1, widget.height) * 1.65f;
        button->AddChild(std::move(image));
        result = std::move(button);
      } else {
        result.reset(new DialogImage(legacyName));
      }
    } else if (widget.WIDGETTYPE == WIDGET_BASIC &&
               (widget.width <= 4 || widget.height <= 4)) {
      std::unique_ptr<UIRectangle> rectangle(new UIRectangle());
      rectangle->subtype = "filled";
      rectangle->fillColor = "2BAAFF";
      result = std::move(rectangle);
    } else {
      std::unique_ptr<UITextParagraph> caption(new UITextParagraph());
      caption->font = "AeroMaticsLightRegular";
      caption->fontSize = 24;
      caption->fillColor = "2BAAFF";
      result = std::move(caption);
    }
    if (!result)
      return result;
    result->name = Text(widget.GetName());
    result->x = 80 + (widget.x - 50) * 1.9f;
    result->y = 316 + (widget.y - 120) * 1.65f;
    result->w = std::max(1, widget.width) * 1.9f;
    result->h = std::max(1, widget.height) * 1.65f;
    if (Editable(widget.WIDGETTYPE)) {
      result->h = std::max(45.0f, result->h);
      if (auto *caption = dynamic_cast<UITextPoint *>(result->FindChild("txtCaption")))
        caption->fontSize = 24;
    }
    return result;
  }

  void UpdateDialog() {
    DialogScreen &dialog = *static_cast<DialogScreen *>(model);
    std::set<DialogScreenWidget *> present;
    for (int index = 0; index < dialog.widgets.Size(); ++index)
      present.insert(dialog.widgets.GetData(index));
    for (auto iterator = widgets.begin(); iterator != widgets.end();) {
      if (present.count(iterator->first))
        ++iterator;
      else {
        RemoveWidget(iterator->second);
        iterator = widgets.erase(iterator);
      }
    }
    for (int index = 0; index < dialog.widgets.Size(); ++index) {
      DialogScreenWidget *widget = dialog.widgets.GetData(index);
      if (!widget)
        continue;
      const std::string legacyName = WidgetButton(*widget, *dialog.GetComputer());
      auto found = widgets.find(widget);
      if (found != widgets.end() &&
          (found->second.type != widget->WIDGETTYPE || found->second.legacyName != legacyName)) {
        RemoveWidget(found->second);
        widgets.erase(found);
        found = widgets.end();
      }
      EnsureWidgetBackend(*widget, *dialog.GetComputer(), legacyName,
                          CurrentRemote()->security_name);
      if (found == widgets.end()) {
        UIObject *control = selectedDialog ? selectedDialog->FindChild(Text(widget->GetName())) : nullptr;
        bool generated = !control;
        // The authored email summaries are paragraphs; the model says editable.
        // Upgrade these to actual HD fields rather than silently losing edits.
        if (control && Editable(widget->WIDGETTYPE) &&
            !dynamic_cast<UIButtonTextField *>(control)) {
          const float x = control->x, y = control->y, w = control->w, h = control->h;
          auto replacement = MakeWidget(*widget, legacyName);
          if (replacement) {
            replacement->x = x; replacement->y = y;
            replacement->w = w; replacement->h = h;
            if (auto *field = dynamic_cast<UIButtonTextField *>(replacement.get()))
              field->multiline = h > 60;
            selectedDialog->DetachChild(control);
            control = replacement.get();
            selectedDialog->AddChild(std::move(replacement));
            generated = false;
          }
        }
        // Thin legacy borders have their authored HD equivalent already.
        if (!control && selectedDialog && widget->WIDGETTYPE == WIDGET_BASIC &&
            (widget->width <= 4 || widget->height <= 4))
          continue;
        if (!control) {
          auto created = MakeWidget(*widget, legacyName);
          if (!created)
            continue;
          control = created.get();
          generic->AddChild(std::move(created));
        }
        SetVisible(control, true);
        if (Editable(widget->WIDGETTYPE))
          AddField(control, legacyName, widget->WIDGETTYPE == WIDGET_PASSWORDBOX);
        if (Action(widget->WIDGETTYPE))
          Bind(control, "widget:" + legacyName);
        found = widgets.emplace(widget, Widget{control, widget->WIDGETTYPE, legacyName, generated}).first;
      }
      UIObject *control = found->second.control;
      if (!Editable(widget->WIDGETTYPE) && widget->WIDGETTYPE != WIDGET_IMAGE &&
          widget->WIDGETTYPE != WIDGET_IMAGEBUTTON &&
          (dynamic_cast<UITextPoint *>(control) || control->FindChild("txtCaption"))) {
        const std::string caption = LegacyHasButton(legacyName.c_str())
            ? LegacyCaption(legacyName.c_str()) : Text(widget->GetCaption());
        SetText(control, caption);
      }
    }
    if (dialogName == "lanScreen") {
      bool isolation = false;
      for (int index = 0; index < dialog.widgets.Size(); ++index) {
        DialogScreenWidget *widget = dialog.widgets.GetData(index);
        if (widget && (widget->data1 == 82 || widget->data1 == 83))
          isolation = true;
      }
      SetVisible(selectedDialog->FindChild("iconBridge"), isolation);
      SetVisible(selectedDialog->FindChild("iconAuth"), !isolation);
    }
    SynchronizeFields();
  }
  void RemoveWidget(const Widget &widget) {
    fields.erase(widget.legacyName);
    if (LegacyHasButton(widget.legacyName.c_str()))
      EclRemoveButton(const_cast<char *>(widget.legacyName.c_str()));
    if (widget.generated && widget.control->parent)
      widget.control->parent->DetachChild(widget.control);
    else {
      SetVisible(widget.control, false);
      widget.control->onClickCallback = nullptr;
      if (auto *field = dynamic_cast<UIButtonTextField *>(widget.control))
        field->onEnterCallback = nullptr;
    }
  }

  void PrepareCodes() {
    const bool userid = type == SCREEN_USERIDSCREEN;
    for (int index = 0; index < 4; ++index) {
      const std::string name = "btnPass" + std::to_string(index);
      if (UIObject *control = Find(name))
        control->parent->DetachChild(control);
    }
    std::unique_ptr<UIScrollableList> list(new UIScrollableList());
    list->name = "knownAccessCodes";
    list->x = userid ? 321 : 361;
    list->y = 811;
    list->w = userid ? 320 : 240;
    list->h = 205;
    list->scrollVertical = true;
    list->scrollHorizontal = false;
    codesList = list.get();
    view->Root()->AddChild(std::move(list));
  }
  void UpdateCodes() {
    std::vector<std::string> codes;
    CollectCodes(&game->GetWorld()->GetPlayer()->codes, model->GetComputer()->ip, codes);
    std::set<std::string> present;
    std::map<std::string, int> occurrences;
    for (std::size_t index = 0; index < codes.size(); ++index) {
      const std::string key = codes[index] + '\n' + std::to_string(occurrences[codes[index]]++);
      present.insert(key);
      auto found = codeRows.find(key);
      if (found == codeRows.end()) {
        HDRepeatLayoutFactory factory(view->LayoutName(), "btnPass0");
        auto row = factory.Create();
        if (!row)
          continue;
        row->x = 0;
        SetText(row.get(), codes[index]);
        Bind(row.get(), "code:" + codes[index]);
        UIObject *pointer = row.get();
        codesList->AddChild(std::move(row));
        found = codeRows.emplace(key, pointer).first;
      }
      found->second->name = "btnPass" + std::to_string(index);
      found->second->y = index * 50;
    }
    for (auto iterator = codeRows.begin(); iterator != codeRows.end();) {
      if (present.count(iterator->first))
        ++iterator;
      else {
        codesList->DetachChild(iterator->second);
        iterator = codeRows.erase(iterator);
      }
    }
    codesList->ClampScroll();
  }

  void UpdateMenu() {
    MenuScreen &menu = *static_cast<MenuScreen *>(model);
    std::set<std::string> present;
    std::map<std::string, int> occurrences;
    RemoteInterface *remote = CurrentRemote();
    for (int index = 0; index < menu.NumOptions(); ++index) {
      // An option's page/caption is stable across insertion/reordering; its
      // current index is only used to resolve the real Eclipse callback.
      const std::string identity = std::to_string(menu.GetNextPage(index)) + ':' + Text(menu.GetCaption(index));
      const std::string key = identity + ':' + std::to_string(occurrences[identity]++);
      present.insert(key);
      auto found = menuRows.find(key);
      if (found == menuRows.end()) {
        HDRepeatLayoutFactory factory("HUD/interfaces/MenuScreen", "btn0");
        auto row = factory.Create();
        if (!row)
          continue;
        row->x = 0;
        UIObject *pointer = row.get();
        menuList->AddChild(std::move(row));
        found = menuRows.emplace(key, pointer).first;
      }
      UIObject *row = found->second;
      row->name = "btn" + std::to_string(index);
      row->y = index * 50;
      SetText(row, Text(menu.GetCaption(index)));
      const bool clearance = remote->security_level <= menu.GetSecurity(index);
      SetEnabled(row, clearance);
      row->alpha = clearance ? 1.0f : 0.4f;
      Bind(row, "menu:" + key);
    }
    for (auto iterator = menuRows.begin(); iterator != menuRows.end();) {
      if (present.count(iterator->first))
        ++iterator;
      else {
        menuList->DetachChild(iterator->second);
        iterator = menuRows.erase(iterator);
      }
    }
    menuList->ClampScroll();
  }

  void UpdateHighSecurity() {
    HighSecurityScreen &screen = *static_cast<HighSecurityScreen *>(model);
    const char *slots[] = {"Pass", "Cypher", "Voice"};
    int worst = 1;
    std::set<std::string> used;
    std::set<std::string> extra;
    for (int index = 0; index < screen.systems.Size(); ++index) {
      MenuScreenOption *option = screen.systems.GetData(index);
      if (!option)
        continue;
      worst = std::max(worst, option->security);
      ComputerScreen *target = screen.GetComputer()->GetComputerScreen(option->nextpage);
      std::string slot;
      if (target && (target->GetOBJECTID() == OID_USERIDSCREEN || target->GetOBJECTID() == OID_PASSWORDSCREEN))
        slot = "Pass";
      else if (target && target->GetOBJECTID() == OID_CYPHERSCREEN)
        slot = "Cypher";
      else if (target && target->GetOBJECTID() == OID_GENERICSCREEN &&
               static_cast<GenericScreen *>(target)->SCREEN_TYPE == SCREEN_VOICEANALYSIS)
        slot = "Voice";
      if (slot.empty() || used.count(slot)) {
        const std::string name = "securitySystem:" + std::to_string(option->nextpage) + ':' + option->caption;
        extra.insert(name);
        UIObject *row = generic->FindChild(name);
        if (!row) {
          HDRepeatLayoutFactory factory("HUD/interfaces/MessageScreen", "btnOK");
          auto created = factory.Create();
          if (!created)
            continue;
          created->name = name;
          created->x = 80;
          created->w = 800;
          row = created.get();
          generic->AddChild(std::move(created));
        }
        row->y = 850 + index * 40;
        SetText(row, std::string(option->caption) + (option->security < 10 ? " - Access granted" : " - Access denied"));
        Bind(row, "system:" + name);
      } else {
        used.insert(slot);
        UIObject *button = Find("btn" + slot);
        SetVisible(button, true);
        SetVisible(Find("txt" + slot), true);
        SetText(Find("txt" + slot), option->caption);
        SetVisible(Find("icon" + slot + "On"), option->security < 10);
        SetVisible(Find("icon" + slot + "Off"), option->security == 10);
        Bind(button, "system:securitySystem:" + std::to_string(option->nextpage) + ':' + option->caption);
      }
    }
    for (const char *slot : slots) {
      if (used.count(slot))
        continue;
      SetVisible(Find("btn" + std::string(slot)), false);
      SetVisible(Find("txt" + std::string(slot)), false);
      SetVisible(Find("icon" + std::string(slot) + "On"), false);
      SetVisible(Find("icon" + std::string(slot) + "Off"), false);
    }
    for (auto iterator = generic->children.begin(); iterator != generic->children.end();) {
      UIObject *row = iterator->get();
      if (extra.count(row->name)) {
        ++iterator;
      } else {
        // DetachChild also releases pointer/focus links to a removed system.
        generic->DetachChild(row);
        iterator = generic->children.begin();
      }
    }
    SetText(Find("btnAccess"), worst < 10 ? "ACCESS GRANTED" : "ACCESS DENIED");
  }

  bool Prepare(RemoteInterface &remote) {
    fields.clear(); widgets.clear(); codeRows.clear(); menuRows.clear();
    generic = selectedDialog = nullptr;
    codesList = menuList = nullptr;
    model = remote.GetComputerScreen();
    legacy = remote.GetInterfaceScreen();
    type = legacy->ScreenID();
    page = remote.currentscreenindex;
    view.reset(new HDLayoutView(owner, std::string("HUD/interfaces/") + LayoutFor(type)));
    if (!view->Load())
      return false;
    UIObject *root = view->Root();
    root->interactive = false;
    root->onClickCallback = nullptr;
    std::unique_ptr<UIContainer> fallback(new UIContainer());
    fallback->name = "dynamicWidgets";
    fallback->w = root->w;
    fallback->h = root->h;
    generic = fallback.get();
    root->AddChild(std::move(fallback));
    switch (type) {
    case SCREEN_MESSAGESCREEN:
      Bind("btnOK", "proceed"); Bind("btnMail", "mail");
      break;
    case SCREEN_DISCONNECTEDSCREEN:
      Bind("btnOK", "proceed");
      // Create consumed/cleared loginslost in the live backend; retain that
      // receipt rather than querying the now-empty static list.
      lostLogins = LegacyCaption("disconnectedscreen_loginslost");
      break;
    case SCREEN_MENUSCREEN: {
      for (int index = 0; index < 10; ++index)
        if (UIObject *button = Find("btn" + std::to_string(index)))
          button->parent->DetachChild(button);
      std::unique_ptr<UIScrollableList> list(new UIScrollableList());
      list->name = "menuOptions";
      list->x = -1; list->y = 316; list->w = 963; list->h = 700;
      list->scrollVertical = true; list->scrollHorizontal = false;
      menuList = list.get();
      root->AddChild(std::move(list));
      break;
    }
    case SCREEN_DIALOGSCREEN:
      dialogName = SelectDialog(*static_cast<DialogScreen *>(model));
      for (auto &child : root->children) {
        if (dynamic_cast<UIContainer *>(child.get()) && child.get() != generic)
          child->visible = child->name == dialogName;
      }
      selectedDialog = dialogName.empty() ? nullptr : Find(dialogName);
      HideControls(selectedDialog);
      break;
    case SCREEN_PASSWORDSCREEN:
    case SCREEN_USERIDSCREEN:
      AddField(Find("btnPassword"), type == SCREEN_PASSWORDSCREEN ? "passwordscreen_password" : "useridscreen_code", true);
      if (type == SCREEN_USERIDSCREEN)
        AddField(Find("btnUsername"), "useridscreen_name", false);
      Bind("btnPassword", "target");
      if (type == SCREEN_USERIDSCREEN)
        Bind("btnUsername", "target");
      Bind("btnSubmit", "proceed"); Bind("btnBypass", "bypass");
      PrepareCodes();
      break;
    case SCREEN_HIGHSECURITYSCREEN:
      Bind("btnAccess", "proceed"); Bind("btnBypass", "bypass");
      break;
    case SCREEN_CYPHERSCREEN: {
      Bind("btnProceed", "proceed"); Bind("btnBypass", "bypass");
      cypherTick = 0;
      cypherLocks = -1;
      cypherDigits.fill('0');
      std::unique_ptr<UIInteractive> target(new UIInteractive());
      target->name = "cypherProgramTarget";
      target->x = 160; target->y = 360; target->w = 640; target->h = 365;
      Bind(target.get(), "target");
      generic->AddChild(std::move(target));
      break;
    }
    }
    return view->Show();
  }

  void Update() {
    RemoteInterface *remote = CurrentRemote();
    if (!remote || !LayoutFor(remote->GetInterfaceScreen()->ScreenID())) {
      Hide();
      return;
    }
    if (!Matches() || !view || !view->Root()) {
      Hide();
      if (!Prepare(*remote))
        return;
    } else if (!view->IsVisible()) {
      view->Show();
    }
    if (UIObject *title = Find("txtTitle")) SetText(title, model->maintitle);
    if (UIObject *subtitle = Find("txtSubtitle")) SetText(subtitle, model->subtitle);
    switch (type) {
    case SCREEN_MESSAGESCREEN: {
      MessageScreen &message = *static_cast<MessageScreen *>(model);
      SetText(Find("txtMessage"), Text(message.textmessage));
      SetVisible(Find("btnOK"), message.buttonmessage != nullptr);
      if (message.buttonmessage) SetText(Find("btnOK"), message.buttonmessage);
      SetVisible(Find("btnMail"), message.mailthistome);
      break;
    }
    case SCREEN_DISCONNECTEDSCREEN:
      SetText(Find("txtMessage"), Text(static_cast<DisconnectedScreen *>(model)->textmessage));
      SetText(Find("txtLoginsLost"), lostLogins);
      SetVisible(Find("txtLoginsLost"), !lostLogins.empty());
      break;
    case SCREEN_DIALOGSCREEN: UpdateDialog(); break;
    case SCREEN_MENUSCREEN: UpdateMenu(); break;
    case SCREEN_PASSWORDSCREEN:
    case SCREEN_USERIDSCREEN:
      SynchronizeFields();
      UpdateCodes();
      if (type == SCREEN_USERIDSCREEN) {
        const std::string message = LegacyCaption("useridscreen_message");
        SetText(Find("txtMessage"), message.empty() ? "Enter your credentials to continue." : message);
      }
      SetVisible(Find("btnBypass"), LegacyHasButton(type == SCREEN_PASSWORDSCREEN ? "passwordscreen_bypass" : "useridscreen_bypass"));
      break;
    case SCREEN_HIGHSECURITYSCREEN:
      UpdateHighSecurity();
      SetVisible(Find("btnBypass"), LegacyHasButton("highsecurity_bypass"));
      break;
    case SCREEN_CYPHERSCREEN: {
      auto *cypher = static_cast<CypherScreenInterface *>(legacy);
      const int total = CYPHER_WIDTH * CYPHER_HEIGHT;
      const int locked = std::min(CYPHER_WIDTH, cypher->NumLocked() * CYPHER_WIDTH / total);
      // GOLD's thirty display cells represent continuous decipher progress;
      // they are not a second independently solvable board.
      if (locked != cypherLocks || ++cypherTick % 5 == 0) {
        for (int index = 0; index < CYPHER_WIDTH; ++index)
          cypherDigits[index] = index < locked ? '0' : '0' + NumberGenerator::RandomNumber(10);
        cypherLocks = locked;
        SetText(Find("txtNumberLine"), std::string(cypherDigits.begin(), cypherDigits.end()));
        SetText(Find("txtNumber"), std::string(1, cypherDigits[std::min(locked, CYPHER_WIDTH - 1)]));
      }
      SetEnabled(Find("btnProceed"), cypher->NumUnLocked() == 0);
      SetVisible(Find("btnBypass"), LegacyHasButton("cypherscreen_bypass"));
      break;
    }
    }
  }

  bool Invoke(const std::string &command) {
    if (!Matches() || !view || !view->IsVisible())
      return false;
    SynchronizeFields();
    RemoteInterface *remote = CurrentRemote();
    if (command == "return") {
      if (type == SCREEN_PASSWORDSCREEN)
        return Invoke("proceed");
      return legacy->ReturnKeyPressed();
    }
    if (command == "escape")
      return legacy->EscapeKeyPressed();
    if (command == "mail" && type == SCREEN_MESSAGESCREEN)
      return LegacyClick("messagescreen_mailme");
    if (command == "target") {
      const char *target = type == SCREEN_PASSWORDSCREEN ? "passwordscreen_password" :
                           type == SCREEN_USERIDSCREEN ? "useridscreen_code" :
                           type == SCREEN_CYPHERSCREEN ? "cypherscreen_cypher" : nullptr;
      return target && LegacyClick(target);
    }
    if (command == "bypass") {
      const char *button = type == SCREEN_PASSWORDSCREEN ? "passwordscreen_bypass" :
                           type == SCREEN_USERIDSCREEN ? "useridscreen_bypass" :
                           type == SCREEN_HIGHSECURITYSCREEN ? "highsecurity_bypass" :
                           type == SCREEN_CYPHERSCREEN ? "cypherscreen_bypass" : nullptr;
      return button && LegacyHasButton(button) && LegacyClick(button);
    }
    if (command == "proceed") {
      if (type == SCREEN_MESSAGESCREEN || type == SCREEN_DISCONNECTEDSCREEN)
        return legacy->ReturnKeyPressed();
      if (type == SCREEN_USERIDSCREEN)
        return LegacyClick("useridscreen_proceed");
      if (type == SCREEN_HIGHSECURITYSCREEN)
        return LegacyClick("highsecurity_proceed");
      if (type == SCREEN_CYPHERSCREEN)
        return LegacyClick("cypherscreen_proceed");
      if (type == SCREEN_PASSWORDSCREEN) {
        if (!game->GetWorld()->GetPlayer()->IsConnected())
          return true;
        PasswordScreen *password = static_cast<PasswordScreen *>(model);
        if (LegacyCaption("passwordscreen_password") == password->password)
          legacy->Update();
        else {
          SetText(Find("txtMessage"), "Invalid Password");
          SgPlaySound(RsArchiveFileOpen("sounds/failure.wav"), "sounds/failure.wav", false);
        }
        return true;
      }
    }
    if (command.compare(0, 7, "widget:") == 0 && type == SCREEN_DIALOGSCREEN) {
      const std::string name = command.substr(7);
      DialogScreen &dialog = *static_cast<DialogScreen *>(model);
      for (int index = 0; index < dialog.widgets.Size(); ++index) {
        DialogScreenWidget *widget = dialog.widgets.GetData(index);
        if (widget && Action(widget->WIDGETTYPE) && WidgetButton(*widget, *dialog.GetComputer()) == name)
          return LegacyClick(name.c_str());
      }
    }
    if (command.compare(0, 5, "menu:") == 0 && type == SCREEN_MENUSCREEN) {
      MenuScreen &menu = *static_cast<MenuScreen *>(model);
      std::map<std::string, int> occurrences;
      for (int index = 0; index < menu.NumOptions(); ++index) {
        const std::string identity = std::to_string(menu.GetNextPage(index)) + ':' + Text(menu.GetCaption(index));
        const std::string key = identity + ':' + std::to_string(occurrences[identity]++);
        if (key == command.substr(5) && remote->security_level <= menu.GetSecurity(index)) {
          const int next = menu.GetNextPage(index);
          if (next != -1)
            remote->RunScreen(next, model->GetComputer());
          return true;
        }
      }
    }
    if (command.compare(0, 7, "system:") == 0 && type == SCREEN_HIGHSECURITYSCREEN) {
      HighSecurityScreen &screen = *static_cast<HighSecurityScreen *>(model);
      for (int index = 0; index < screen.systems.Size(); ++index) {
        MenuScreenOption *option = screen.systems.GetData(index);
        if (option && command.substr(7) == "securitySystem:" + std::to_string(option->nextpage) + ':' + option->caption) {
          const std::string button = "highsecurity_systemtitle " + std::to_string(index);
          return LegacyClick(button.c_str());
        }
      }
    }
    if (command.compare(0, 5, "code:") == 0 &&
        (type == SCREEN_PASSWORDSCREEN || type == SCREEN_USERIDSCREEN)) {
      const std::string code = command.substr(5);
      std::vector<std::string> current;
      CollectCodes(&game->GetWorld()->GetPlayer()->codes, model->GetComputer()->ip, current);
      if (std::find(current.begin(), current.end(), code) == current.end())
        return false;
      const auto parts = Quoted(code);
      if (type == SCREEN_PASSWORDSCREEN && !parts.empty())
        LegacySetCaption("passwordscreen_password", parts.front());
      else if (type == SCREEN_USERIDSCREEN && (parts.size() == 1 || parts.size() == 2)) {
        LegacySetCaption("useridscreen_name", parts.size() == 1 ? "admin" : parts.front());
        LegacySetCaption("useridscreen_code", parts.back());
      } else
        return false;
      SynchronizeFields();
      return true;
    }
    return false;
  }
};

HDRemoteCore::HDRemoteCore(UIObject &root) : state(new State(root)) {}
HDRemoteCore::~HDRemoteCore() = default;
void HDRemoteCore::Update() { state->Update(); }
void HDRemoteCore::Hide() { state->Hide(); }
bool HDRemoteCore::IsVisible() const { return state->view && state->view->IsVisible(); }
bool HDRemoteCore::Invoke(const std::string &command) {
  const bool invoked = state->Invoke(command);
  if (state->view && !state->Matches())
    state->view->Hide();
  return invoked;
}

bool HDRemoteCore::HandleInput(const ALLEGRO_EVENT &event) {
  if (!IsVisible()) {
    RemoteInterface *remote = CurrentRemote();
    if (!state->view || !state->view->Root() || state->Matches() || !remote ||
        !LayoutFor(remote->GetInterfaceScreen()->ScreenID()))
      return false;
  }
  UIObject *surface = state->view->Root();
  const bool key = event.type == ALLEGRO_EVENT_KEY_DOWN ||
                   event.type == ALLEGRO_EVENT_KEY_UP || event.type == ALLEGRO_EVENT_KEY_CHAR;
  if (key) {
    UIObject *focus = state->owner.GetFocus();
    const bool ownsFocus = focus && focus->IsDescendantOf(surface);
    if (focus && !ownsFocus)
      return false;
    const int code = event.keyboard.keycode;
    auto *field = dynamic_cast<UIButtonTextField *>(focus);
    if ((code == ALLEGRO_KEY_ENTER || code == ALLEGRO_KEY_PAD_ENTER) &&
        field && field->multiline && state->Matches() &&
        state->type == SCREEN_DIALOGSCREEN &&
        !static_cast<DialogScreen *>(state->model)->returnkeybutton &&
        !(event.keyboard.modifiers & ALLEGRO_KEYMOD_CTRL)) {
      state->owner.DispatchEvent(event);
      state->SynchronizeFields();
      return true;
    }
    if (code == ALLEGRO_KEY_ENTER || code == ALLEGRO_KEY_PAD_ENTER ||
        code == ALLEGRO_KEY_ESCAPE) {
      if (event.type == ALLEGRO_EVENT_KEY_DOWN && state->Matches())
        Invoke(code == ALLEGRO_KEY_ESCAPE ? "escape" : "return");
      return true;
    }
    if (!ownsFocus)
      return false;
    if (state->Matches()) {
      state->owner.DispatchEvent(event);
      state->SynchronizeFields();
    }
    return true;
  }
  const bool pointer = event.type == ALLEGRO_EVENT_MOUSE_AXES ||
                       event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN ||
                       event.type == ALLEGRO_EVENT_MOUSE_BUTTON_UP;
  if (pointer) {
    UIObject *capture = state->owner.GetPointerCapture();
    UIObject *workspace =
        surface->FindChildRecursive("legacyWorkspaceBackdrop");
    if ((workspace && workspace->ContainsPoint(event.mouse.x, event.mouse.y)) ||
        (capture && capture->IsDescendantOf(surface))) {
      if (state->Matches())
        state->owner.DispatchEvent(event);
      return true;
    }
  }
  return false;
}

} // namespace HDUI
