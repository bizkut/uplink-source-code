#include "hd_mm_options.h"
#include "hd_layout_parser.h"
#include "app/app.h"
#include "options/options.h"
#include "gucci.h"
#include "soundgarden.h"
#include <algorithm>
#include <cstdio>
#include <utility>

namespace HDUI {
namespace {
struct ScreenModesDeleter {
  void operator()(GciScreenModeList *modes) const {
    if (modes) {
      GciDeleteScreenModeArrayData(modes);
      delete modes;
    }
  }
};

struct ProfilesDeleter {
  void operator()(DArray<char *> *profiles) const {
    if (!profiles)
      return;
    for (int i = 0; i < profiles->Size(); ++i)
      if (profiles->ValidIndex(i))
        delete[] profiles->GetData(i);
    delete profiles;
  }
};

template <typename T>
T *Control(UIObject &tree, const char *path) {
  return dynamic_cast<T *>(tree.FindChildRecursive(path));
}

void SetControlEnabled(UIObject &control, bool enabled) {
  control.enabled = enabled;
  control.focusable = enabled;
  control.isPressed = false;
  control.isHovered = false;
}
} // namespace

HDMMOptions::HDMMOptions(UIObject &root, ProfileSelected selected,
                         NewUserRequested newUser,
                         ProfileRetired retired)
    : ownerRoot(root), profileSelected(std::move(selected)),
      newUserRequested(std::move(newUser)), profileRetired(std::move(retired)),
      panel(nullptr), popup(nullptr), resolution(nullptr),
      resolutionCaption(nullptr), fullscreen(nullptr), music(nullptr),
      softwareMouse(nullptr), selectedResolution(0), popupDismissKey(-1) {}

HDMMOptions::~HDMMOptions() { Hide(); }

bool HDMMOptions::LoadLayouts() {
  if (panel)
    return true;
  std::unique_ptr<UIObject> options =
      LayoutManager::GetInstance().TakeLayout("MainMenu/MMOptions");
  std::unique_ptr<UIObject> confirmation =
      LayoutManager::GetInstance().TakeLayout("PopUp");
  if (!options || !confirmation) {
    std::fprintf(stderr, "HD_UI: Cannot load MainMenu/MMOptions.xml or PopUp.xml\n");
    return false;
  }

  UIButtonStandard *apply = Control<UIButtonStandard>(*options, "btnApply");
  UIButtonStandard *newUser = Control<UIButtonStandard>(*options, "btnNewUser");
  UIButtonStandard *resolutionButton =
      Control<UIButtonStandard>(*options, "btnResolution");
  UITextPoint *resolutionText =
      Control<UITextPoint>(*options, "btnResolution/txtCaption");
  UIButtonSelect *fullscreenButton =
      Control<UIButtonSelect>(*options, "btnFrameless");
  UIButtonSelect *musicButton = Control<UIButtonSelect>(*options, "btnMusic");
  UIButtonSelect *mouseButton = Control<UIButtonSelect>(*options, "btnSWMouse");
  UIButtonStandard *ok =
      Control<UIButtonStandard>(*confirmation, "dialogueBox/btnOK");
  UIButtonStandard *cancel =
      Control<UIButtonStandard>(*confirmation, "dialogueBox/btnCancel");
  UITextPoint *title = Control<UITextPoint>(*confirmation, "dialogueBox/txtTitle");
  UITextParagraph *contents =
      Control<UITextParagraph>(*confirmation, "dialogueBox/txtContents");
  if (!apply || !newUser || !resolutionButton || !resolutionText ||
      !fullscreenButton || !musicButton || !mouseButton || !ok || !cancel ||
      !title || !contents || !confirmation->FindChild("overlay")) {
    std::fprintf(stderr, "HD_UI: Main-menu options or confirmation controls are missing\n");
    return false;
  }

  std::array<ProfileRow, 4> profileRows;
  for (std::size_t i = 0; i < profileRows.size(); ++i) {
    char name[16];
    std::snprintf(name, sizeof(name), "user%u", static_cast<unsigned>(i + 1));
    ProfileRow &row = profileRows[i];
    row.container = options->FindChild(name);
    if (row.container) {
      row.caption = Control<UITextPoint>(*row.container, "txtName");
      row.select = Control<UIButtonStandard>(*row.container, "btnBase");
      row.remove = Control<UIButtonStandard>(*row.container, "btnDelete");
    }
    if (!row.container || !row.caption || !row.select || !row.remove) {
      std::fprintf(stderr, "HD_UI: Main-menu options profile row %s is incomplete\n", name);
      return false;
    }
  }

  panel = options.get();
  popup = confirmation.get();
  panelTree = std::move(options);
  popupTree = std::move(confirmation);
  rows = std::move(profileRows);
  resolution = resolutionButton;
  resolutionCaption = resolutionText;
  fullscreen = fullscreenButton;
  music = musicButton;
  softwareMouse = mouseButton;
  panel->visible = false;
  panel->interactive = true;
  popup->visible = false;
  popup->interactive = true;

  // GOLD sub_477D20/sub_4F6E50 use btnFrameless as the fullscreen switch.
  if (UITextPoint *caption = Control<UITextPoint>(*fullscreen, "txtCaption"))
    caption->text = "Fullscreen";
  apply->onClickCallback = [this](UIObject *) { Apply(); };
  newUser->onClickCallback = [this](UIObject *) { RequestNewUser(); };
  resolution->onClickCallback = [this](UIObject *) { CycleResolution(); };
  fullscreen->onClickCallback = [this](UIObject *) { UpdateResolution(); };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    rows[i].select->onClickCallback = [this, i](UIObject *) { SelectProfile(i); };
    rows[i].remove->onClickCallback = [this, i](UIObject *) { ConfirmRemoval(i); };
  }
  ok->onClickCallback = [this](UIObject *) { RemoveProfile(); };
  cancel->onClickCallback = [this](UIObject *) { HidePopup(); };
  title->text = "ARE YOU SURE?";
  return true;
}

void HDMMOptions::SnapshotSettings() {
  Options *options = app->GetOptions();
  Settings current;
  current.width = options->GetEffectiveOptionValue("graphics_screenwidth");
  current.height = options->GetEffectiveOptionValue("graphics_screenheight");
  current.fullscreen =
      options->GetEffectiveOptionValue("graphics_fullscreen") != 0;
  current.music = options->GetOptionValue("sound_musicenabled") != 0;
  current.softwareMouse = options->GetOptionValue("graphics_softwaremouse") != 0;
  snapshot = current;
  fullscreen->selected = snapshot.fullscreen;
  music->selected = snapshot.music;
  softwareMouse->selected = snapshot.softwareMouse;
}

void HDMMOptions::PopulateResolutions() {
  std::unique_ptr<GciScreenModeList, ScreenModesDeleter> modes(GciListScreenModes());
  screenModes.clear();
  screenModes.reserve(modes ? static_cast<std::size_t>(modes->NumUsed()) + 1 : 1);
  if (modes) {
    for (int i = 0; i < modes->Size(); ++i) {
      if (!modes->ValidIndex(i))
        continue;
      const GciScreenMode *mode = modes->GetData(i);
      if (!mode || !mode->w || !mode->h)
        continue;
      const std::pair<int, int> dimensions(static_cast<int>(mode->w),
                                            static_cast<int>(mode->h));
      if (std::find(screenModes.begin(), screenModes.end(), dimensions) == screenModes.end())
        screenModes.push_back(dimensions);
    }
  }
  const std::pair<int, int> current(snapshot.width, snapshot.height);
  const auto found = std::find(screenModes.begin(), screenModes.end(), current);
  if (found == screenModes.end()) {
    selectedResolution = screenModes.size();
    screenModes.push_back(current);
  } else {
    selectedResolution = static_cast<std::size_t>(found - screenModes.begin());
  }
  UpdateResolution();
}

void HDMMOptions::UpdateResolution() {
  const std::pair<int, int> &mode = screenModes[selectedResolution];
  char caption[64];
  std::snprintf(caption, sizeof(caption), "%dx%d", mode.first, mode.second);
  resolutionCaption->text = caption;
  // No dropdown exists in the current core: each click cycles one deduplicated
  // WIDTHxHEIGHT choice, retaining the actual width/height pair for Apply.
  SetControlEnabled(*resolution, !fullscreen->selected && screenModes.size() > 1);
  resolution->alpha = resolution->enabled ? 1.0f : 0.7f;
}

void HDMMOptions::CycleResolution() {
  if (!IsVisible() || !resolution->IsEnabled())
    return;
  selectedResolution = (selectedResolution + 1) % screenModes.size();
  UpdateResolution();
}

void HDMMOptions::RefreshProfiles() {
  std::unique_ptr<DArray<char *>, ProfilesDeleter> profiles(App::ListExistingGames());
  for (std::size_t i = 0; i < rows.size(); ++i) {
    ProfileRow &row = rows[i];
    // Preserve ListExistingGames order and slot indices, exactly as sub_479050.
    const char *name = profiles && profiles->ValidIndex(static_cast<int>(i))
                           ? profiles->GetData(static_cast<int>(i))
                           : nullptr;
    row.username = name ? name : "";
    const bool occupied = !row.username.empty();
    row.caption->text = occupied ? row.username : "<ENCRYPTED>";
    row.container->alpha = occupied ? 1.0f : 0.7f;
    SetControlEnabled(*row.select, occupied);
    SetControlEnabled(*row.remove, occupied);
  }
}

std::string HDMMOptions::CurrentUsername() const {
  // The selected profile is the current login caption, not necessarily the
  // last profile that completed a sign-in. The callbacks own login mutation.
  if (UIObject *signIn = ownerRoot.FindChildRecursive("signIn"))
    if (UITextPoint *username = Control<UITextPoint>(*signIn, "txtUsername"))
      return username->text;
  if (!selectedProfile.empty())
    return selectedProfile;
  if (app && app->GetOptions())
    if (Option *lastUser = app->GetOptions()->GetOption("game_lastuser"))
      if (lastUser->tooltip[0])
        return lastUser->tooltip;
  for (const ProfileRow &row : rows)
    if (!row.username.empty())
      return row.username;
  return "";
}

bool HDMMOptions::Show() {
  if (!app || !app->GetOptions() || !LoadLayouts())
    return false;
  if (IsVisible()) {
    panel->BringToFront();
    if (popup->parent == &ownerRoot)
      popup->BringToFront();
    return true;
  }
  SnapshotSettings();
  PopulateResolutions();
  RefreshProfiles();
  selectedProfile = CurrentUsername();
  panel->x = 0;
  panel->y = 0;
  panel->visible = true;
  panel->enabled = true;
  ownerRoot.AddChild(std::move(panelTree));
  panel->BringToFront();
  return true;
}

void HDMMOptions::HidePopup() {
  if (!popup || popup->parent != &ownerRoot)
    return;
  popup->visible = false;
  // Retain rather than destroy: this may be the popup's own click callback.
  popupTree = ownerRoot.DetachChild(popup);
  panel->enabled = true;
  removalUsername.clear();
}

void HDMMOptions::Hide() {
  HidePopup();
  if (!panel || panel->parent != &ownerRoot)
    return;
  panel->visible = false;
  panelTree = ownerRoot.DetachChild(panel);
}

bool HDMMOptions::IsVisible() const {
  return panel && panel->parent == &ownerRoot && panel->IsVisible();
}

bool HDMMOptions::IsPopupVisible() const {
  return popup && popup->parent == &ownerRoot && popup->IsVisible();
}

bool HDMMOptions::HandleInput(const ALLEGRO_EVENT &event) {
  const bool keyboard = event.type == ALLEGRO_EVENT_KEY_DOWN ||
                        event.type == ALLEGRO_EVENT_KEY_CHAR ||
                        event.type == ALLEGRO_EVENT_KEY_UP;
  if (keyboard && popupDismissKey == event.keyboard.keycode) {
    if (event.type == ALLEGRO_EVENT_KEY_UP)
      popupDismissKey = -1;
    return true;
  }
  if (!IsPopupVisible())
    return false;
  if (keyboard) {
    if (event.type == ALLEGRO_EVENT_KEY_DOWN) {
      UIObject *cancel = popup->FindChildRecursive("dialogueBox/btnCancel");
      UIObject *confirm = popup->FindChildRecursive("dialogueBox/btnOK");
      const int key = event.keyboard.keycode;
      if (key == ALLEGRO_KEY_ESCAPE) {
        popupDismissKey = key;
        HidePopup();
      } else if (key == ALLEGRO_KEY_TAB && cancel && confirm) {
        ownerRoot.SetFocus(ownerRoot.GetFocus() == cancel ? confirm : cancel);
      } else if (key == ALLEGRO_KEY_ENTER ||
                 key == ALLEGRO_KEY_PAD_ENTER ||
                 key == ALLEGRO_KEY_SPACE) {
        popupDismissKey = key;
        if (confirm && ownerRoot.GetFocus() == confirm)
          RemoveProfile();
        else
          HidePopup();
      }
    }
    return true;
  }
  if (event.type == ALLEGRO_EVENT_MOUSE_AXES ||
      event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN ||
      event.type == ALLEGRO_EVENT_MOUSE_BUTTON_UP) {
    ownerRoot.DispatchEvent(event);
    return true;
  }
  return false;
}

void HDMMOptions::SelectProfile(std::size_t row) {
  if (!IsVisible() || !panel->IsEnabled() || rows[row].username.empty())
    return;
  const std::string username = rows[row].username;
  selectedProfile = username;
  if (profileSelected)
    profileSelected(username, "PASSWORD70RZ");
  Hide();
}

void HDMMOptions::RequestNewUser() {
  if (!IsVisible() || !panel->IsEnabled())
    return;
  Hide();
  if (newUserRequested)
    newUserRequested();
}

void HDMMOptions::ConfirmRemoval(std::size_t row) {
  if (!IsVisible() || !panel->IsEnabled() || rows[row].username.empty())
    return;
  removalUsername = rows[row].username;
  Control<UITextParagraph>(*popup, "dialogueBox/txtContents")->text =
      "Are you sure you want to permanently remove the user '" + removalUsername +
      "'?\nThis cannot be undone.";
  popup->x = 0;
  popup->y = 0;
  popup->w = ownerRoot.w > 0 ? ownerRoot.w : 1920;
  popup->h = ownerRoot.h > 0 ? ownerRoot.h : 1080;
  UIObject *overlay = popup->FindChild("overlay");
  overlay->w = popup->w;
  overlay->h = popup->h;
  popup->visible = true;
  ownerRoot.AddChild(std::move(popupTree));
  UIObject *dialogue = popup->FindChild("dialogueBox");
  const Geometry outer = popup->GetGeometry();
  const Geometry inner = dialogue->GetGeometry();
  dialogue->x = outer.sx > 0 ? std::max(0.0f, (outer.w - inner.w) / (2 * outer.sx)) : 0;
  dialogue->y = outer.sy > 0 ? std::max(0.0f, (outer.h - inner.h) / (2 * outer.sy)) : 0;
  panel->enabled = false;
  popup->BringToFront();
  ownerRoot.SetFocus(dialogue->FindChild("btnCancel"));
}

void HDMMOptions::RemoveProfile() {
  if (!popup || popup->parent != &ownerRoot || removalUsername.empty())
    return;
  const std::string username = removalUsername;
  const bool wasSelected = CurrentUsername() == username;
  Option *lastUser = app->GetOptions()->GetOption("game_lastuser");
  const bool wasLastUser = lastUser && username == lastUser->tooltip;
  HidePopup();
  if (profileRetired)
    profileRetired(username);
  // RetireGame also removes this profile's _hotkeys.xml and _notes.xml files.
  app->RetireGame(const_cast<char *>(username.c_str()));
  RefreshProfiles();
  if (wasSelected || wasLastUser) {
    std::string replacement;
    for (const ProfileRow &row : rows)
      if (!row.username.empty()) {
        replacement = row.username;
        break;
      }
    if (wasSelected) {
      selectedProfile = replacement;
      if (profileSelected)
        profileSelected(replacement, replacement.empty() ? "" : "PASSWORD70RZ");
    }
    if (wasLastUser) {
      char empty[] = "";
      app->GetOptions()->SetOptionValue(
          "game_lastuser", 0, replacement.empty() ? empty : replacement.data(),
          false, false);
      app->GetOptions()->SetOptionValue(
          "game_lastuserdate", 0, empty, false, false);
      app->GetOptions()->Save(nullptr);
    }
  }
}

void HDMMOptions::Apply() {
  if (!IsVisible() || !panel->IsEnabled())
    return;
  Settings committed;
  committed.width = screenModes[selectedResolution].first;
  committed.height = screenModes[selectedResolution].second;
  committed.fullscreen = fullscreen->selected;
  committed.music = music->selected;
  committed.softwareMouse = softwareMouse->selected;
  Options *options = app->GetOptions();
  // As in GraphicOptionsInterface, leave live display dimensions/fullscreen
  // untouched until App::Close applies shutdown changes and saves them.
  if (committed.width != snapshot.width || committed.height != snapshot.height) {
    options->RequestShutdownChange("graphics_screenwidth", committed.width);
    options->RequestShutdownChange("graphics_screenheight", committed.height);
  }
  if (committed.fullscreen != snapshot.fullscreen)
    options->RequestShutdownChange("graphics_fullscreen", committed.fullscreen);
  if (committed.music != snapshot.music) {
    options->SetOptionValue("sound_musicenabled", committed.music);
    if (committed.music)
      SgPlaylist_Play("main");
    else
      SgPlaylist_Stop();
  }
  if (committed.softwareMouse != snapshot.softwareMouse)
    options->SetOptionValue("graphics_softwaremouse", committed.softwareMouse);
  options->Save(nullptr);
  snapshot = committed;
  Hide();
}

} // namespace HDUI
