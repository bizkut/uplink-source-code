#include "hd_end_screens.h"

#include "app/app.h"
#include "game/game.h"
#include "game/gameobituary.h"
#include "mainmenu/mainmenu.h"
#include "world/generator/plotgenerator.h"
#include "world/rating.h"
#include <algorithm>
#include <cstdio>
#include <limits>
#include <memory>
#include <utility>

namespace HDUI {
namespace {
unsigned CountBits(unsigned bits) {
  unsigned count = 0;
  for (; bits; bits &= bits - 1)
    ++count;
  return count;
}

bool HasCaption(const std::string &caption) {
  return caption.find_first_not_of(" \t\r\n") != std::string::npos;
}

const char *EndingCaption(int screen) {
  switch (screen) {
  case MAINMENU_DISAVOWED: return "disavowed";
  case MAINMENU_DEMOGAMEOVER: return "demogameover";
  case MAINMENU_WAREZGAMEOVER: return "warezgameover";
  case MAINMENU_REVELATIONWON: return "revelationwon";
  case MAINMENU_REVELATIONLOST: return "revelationlost";
  default: return nullptr;
  }
}

// Keep the final paragraph's typography and viewport, but do not lose the long
// Revelation/demo/warez variants beyond its last visible line.
void SetEndingText(HDLayoutView &view, const std::string &caption) {
  auto *text = view.FindAs<UITextParagraph>("txtMessage");
  auto *scroll = view.FindAs<UIScrollableList>("messageScroll");
  if (!text || !scroll || text->text == caption)
    return;
  text->text = caption;
  text->PrepareText();
  ALLEGRO_FONT *font = text->GetFont();
  const float advance = font ? std::max(1.0f, static_cast<float>(
      al_get_font_line_height(font)) + text->leading) : text->fontSize;
  text->h = std::max(scroll->h, advance * text->lines.size());
  scroll->SetScroll(0, 0);
}
} // namespace

HDEndScreens::HDEndScreens(UIObject &root)
    : ownerRoot(root), loading(root, "Loading"), obituary(root, "Obituary"),
      gameOver(root, "GameOver"), connectionLost(root, "ConnectionLost"),
      team(root, "TheTeam"), popup(root), activeView(nullptr),
      activeMenu(nullptr), errorMenu(nullptr),
      activeScreen(MAINMENU_UNKNOWN), observedScreen(MAINMENU_UNKNOWN),
      popupKind(PopupKind::None), awardBits(0), awardsInitialized(false),
      returnReady(false) {
  for (HDLayoutView *view : {&loading, &obituary, &gameOver, &connectionLost,
                             &team})
    view->SetCaptureInput(true);
}

HDEndScreens::~HDEndScreens() { Hide(); }

HDLayoutView *HDEndScreens::ViewForScreen(int screen) {
  switch (screen) {
  case MAINMENU_LOADING: return &loading;
  case MAINMENU_OBITUARY: return &obituary;
  case MAINMENU_CONNECTIONLOST: return &connectionLost;
  case MAINMENU_THETEAM: return &team;
  case MAINMENU_DISAVOWED:
  case MAINMENU_DEMOGAMEOVER:
  case MAINMENU_WAREZGAMEOVER:
  case MAINMENU_REVELATIONWON:
  case MAINMENU_REVELATIONLOST:
    return &gameOver;
  default: return nullptr;
  }
}

bool HDEndScreens::Prepare(HDLayoutView &view) {
  if (!view.Load())
    return false;
  UIObject *root = view.Root();
  if (!root->FindChild("legacyEndBackdrop")) {
    // MainMenu keeps the Eclipse ending screen alive as the model/callback
    // backend.  Its text must not bleed through the full-screen GOLD layout.
    std::unique_ptr<UIRectangle> backdrop(new UIRectangle());
    backdrop->name = "legacyEndBackdrop";
    backdrop->x = 0;
    backdrop->y = 0;
    backdrop->w = root->w;
    backdrop->h = root->h;
    backdrop->fillColor = "000000";
    backdrop->fillAlpha = 1.0f;
    backdrop->interactive = false;
    root->children.insert(root->children.begin(), std::move(backdrop));
    root->children.front()->parent = root;
  }
  root->interactive = true;
  root->focusable = true;
  HDLayoutView *current = &view;
  root->onClickCallback = [current](UIObject *) { current->BringToFront(); };

  if (&view == &loading)
    return view.FindAs<UITextParagraph>("loadingElements/txtCaption");

  UIButtonStandard *back = view.FindAs<UIButtonStandard>("btnBack");
  if (!back)
    return false;
  back->onClickCallback = [this](UIObject *) { Back(); };

  if (&view == &obituary) {
    static const char *fields[] = {
        "contents/txtName", "contents/txtDisavow", "contents/txtFinanceVal",
        "contents/txtURatingVal", "contents/txtNRatingVal", "contents/txtScoreVal",
        "contents/txtLives", "contents/txtDestroyed", "contents/txtSecurity"};
    for (const char *field : fields)
      if (!view.FindAs<UITextPoint>(field))
        return false;
    UIObject *count = view.FindAs<UITextPoint>("contents/txtAwardsVal");
    UIButtonStandard *pi = view.FindAs<UIButtonStandard>("btnPI");
    if (!count || !pi)
      return false;
    count->interactive = count->focusable = true;
    count->w = 260;
    count->h = 30;
    count->onClickCallback = [this](UIObject *) { ShowAwards(); };
    pi->onClickCallback = [this](UIObject *) { ShowTeam(); };
#ifdef DEMOGAME
    SetVisible(pi, false);
#else
    SetVisible(pi, true);
#endif
    for (unsigned bit = 0; bit < 12; ++bit) {
      char path[64];
      std::snprintf(path, sizeof(path), "contents/awards/iconAw%u", bit);
      auto *button = view.FindAs<UIButtonStandard>(path);
      if (!button)
        return false;
      char image[24];
      std::snprintf(image, sizeof(image), "iconAw%u", bit);
      if (!dynamic_cast<UIImage *>(button->FindChild(image)))
        return false;
      button->onClickCallback = [this, bit](UIObject *) { ShowAward(bit); };
    }
    awardsInitialized = false;
  } else if (&view == &gameOver) {
    auto *text = view.FindAs<UITextParagraph>("txtMessage");
    auto *screen = view.FindAs<UIButtonStandard>("btnScreen");
    if (!text || !screen)
      return false;
    if (disavowedMessage.empty())
      disavowedMessage = text->text;
    screen->onClickCallback = [this](UIObject *) { Back(); };
    if (!view.Find("messageScroll", false)) {
      std::unique_ptr<UIScrollableList> scroll(new UIScrollableList());
      scroll->name = "messageScroll";
      scroll->x = text->x;
      scroll->y = text->y;
      scroll->w = text->w;
      scroll->h = text->h;
      scroll->scalingType = text->scalingType;
      scroll->dragScroll = false;
      scroll->scrollHorizontal = false;
      scroll->scrollVertical = true;
      scroll->focusable = true;
      scroll->onClickCallback = screen->onClickCallback;
      std::unique_ptr<UIObject> paragraph = text->parent->DetachChild(text);
      text->x = text->y = 0;
      scroll->AddChild(std::move(paragraph));
      root->AddChild(std::move(scroll));
      // The full-screen hit target must remain below the message viewport and
      // Back. The latter never overlaps the viewport.
    }
    if (activeScreen == MAINMENU_DISAVOWED)
      SetEndingText(view, disavowedMessage);
    if (auto *scroll = view.FindAs<UIScrollableList>("messageScroll"))
      scroll->SetScroll(0, 0);
  } else if (&view == &connectionLost) {
    if (!view.FindAs<UITextPoint>("txtCaption"))
      return false;
  } else if (&view == &team) {
    // All five final entries are authored in the XML, including Tudor. The
    // legacy hover-only four-person presentation must not overwrite that copy.
    for (int entry = 1; entry <= 5; ++entry) {
      char path[48];
      std::snprintf(path, sizeof(path), "contents/txtName%d", entry);
      if (!view.FindAs<UITextPoint>(path))
        return false;
      std::snprintf(path, sizeof(path), "contents/txtCaption%d", entry);
      if (!view.FindAs<UITextParagraph>(path))
        return false;
      std::snprintf(path, sizeof(path), "contents/icon%d", entry);
      if (!view.FindAs<UIImage>(path))
        return false;
    }
  }
  return true;
}

void HDEndScreens::HideScreen() {
  if (activeView)
    activeView->Hide();
  activeView = nullptr;
  activeMenu = nullptr;
  activeScreen = MAINMENU_UNKNOWN;
  returnReady = false;
}

void HDEndScreens::Hide() {
  popup.Hide();
  popupKind = PopupKind::None;
  errorMenu = nullptr;
  HideScreen();
  observedScreen = MAINMENU_UNKNOWN;
}

bool HDEndScreens::IsVisible() const {
  return popup.IsVisible() || (activeView && activeView->IsVisible());
}

void HDEndScreens::FocusBack() {
  if (!activeView || !activeView->IsVisible() || popup.IsVisible())
    return;
  UIObject *back = returnReady ? activeView->Find("btnBack", false) : nullptr;
  ownerRoot.SetFocus(back ? back : activeView->Root());
}

void HDEndScreens::Update() {
  if (!app || app->Closed()) {
    Hide();
    return;
  }
  MainMenu *menu = app->GetMainMenu();
  const int screen = menu->InScreen();
  MainMenuScreen *current = menu->IsVisible() ? menu->GetMenuScreen() : nullptr;
  const bool failedLoad = observedScreen == MAINMENU_LOADING &&
                          screen == MAINMENU_LOGIN;
  observedScreen = screen;
  HDLayoutView *next = ViewForScreen(screen);

  if (activeView && (screen != activeScreen || current != activeMenu)) {
    HideScreen();
    if (popupKind == PopupKind::Award) {
      popup.Hide();
      popupKind = PopupKind::None;
    }
  }
  if (!next) {
    // FIRSTLOAD belongs to HDIntro; UNKNOWN may be a loaded or paused HUD.
    // Do not invent a timer or start/load a second Game at either handoff.
    UpdateLoadError(failedLoad, current);
    return;
  }
  if (popupKind == PopupKind::LoadError) {
    popup.Hide();
    popupKind = PopupKind::None;
    errorMenu = nullptr;
  }
  if (!activeView || !activeView->IsVisible()) {
    activeScreen = screen;
    activeMenu = current;
    if (!Prepare(*next) || !next->Show()) {
      HideScreen();
      return;
    }
    activeView = next;
    returnReady = screen == MAINMENU_OBITUARY || screen == MAINMENU_THETEAM;
    FocusBack();
  }

  switch (screen) {
  case MAINMENU_LOADING: UpdateLoading(); break;
  case MAINMENU_OBITUARY: UpdateObituary(); break;
  case MAINMENU_CONNECTIONLOST: UpdateConnectionLost(); break;
  case MAINMENU_THETEAM: break;
  default: UpdateGameOver(); break;
  }
}

void HDEndScreens::UpdateLoading() {
  // Login already schedules script 91 (or test-new-game script 90). Mirror its
  // current caption; never call App::LoadGame or re-register that timer here.
  const std::string caption = LegacyHasButton("loading_text")
      ? LegacyCaption("loading_text") : "Connecting to GATEWAY...";
  SetText(loading.Find("loadingElements/txtCaption"), caption);
}

void HDEndScreens::UpdateObituary() {
  if (!game)
    return;
  GameObituary *record = game->GetObituary();
  SetText(obituary.Find("contents/txtName"), std::string("Agent ") + record->name);
  const char *reason = record->GameOverReason();
  SetText(obituary.Find("contents/txtDisavow"), reason ? reason : "");
  SetText(obituary.Find("contents/txtFinanceVal"),
          std::to_string(record->money) + " credits");
  SetText(obituary.Find("contents/txtURatingVal"),
          Rating::GetUplinkRatingString(record->uplinkrating));
  SetText(obituary.Find("contents/txtNRatingVal"),
          Rating::GetNeuromancerRatingString(record->neuromancerrating));
  SetText(obituary.Find("contents/txtScoreVal"), std::to_string(record->score));
  SetText(obituary.Find("contents/txtLives"), "You ruined the lives of " +
          std::to_string(record->score_peoplefucked) + " people");
  SetText(obituary.Find("contents/txtDestroyed"), "You destroyed " +
          std::to_string(record->score_systemsfucked) + " computers");
  SetText(obituary.Find("contents/txtSecurity"), "You compromised " +
          std::to_string(record->score_highsecurityhacks) + " high security systems");

  const unsigned bits = static_cast<unsigned>(record->specialmissionscompleted);
  if (awardsInitialized && bits == awardBits)
    return;
  awardBits = bits;
  awardsInitialized = true;
  const unsigned named = CountBits(bits & 0xfffu);
  const unsigned extra = CountBits(bits & ~0xfffu);
  std::string count = std::to_string(named) + "/12";
  if (extra)
    count += " +" + std::to_string(extra);
  SetText(obituary.Find("contents/txtAwardsVal"), count);
  for (unsigned bit = 0; bit < 12; ++bit) {
    char path[64];
    std::snprintf(path, sizeof(path), "contents/awards/iconAw%u", bit);
    UIObject *button = obituary.Find(path);
    char child[24];
    std::snprintf(child, sizeof(child), "iconAw%u", bit);
    auto *image = button ? dynamic_cast<UIImage *>(button->FindChild(child)) : nullptr;
    if (!image)
      continue;
    char file[32];
    if (bits & (1u << bit))
      std::snprintf(file, sizeof(file), "iconAw%02u_24px.png", bit);
    else
      std::snprintf(file, sizeof(file), "iconAwNone_24px.png");
    if (image->file != file) {
      image->file = file;
      image->LoadBitmap(""); // Award images are entries in the loaded HD atlas.
    }
  }
}

const char *HDEndScreens::ReturnButton() const {
  switch (activeScreen) {
  case MAINMENU_OBITUARY: return "obituary_mainmenu";
  case MAINMENU_THETEAM: return "theteam_exit";
  case MAINMENU_CONNECTIONLOST: return "connectionlost_mainmenu";
  case MAINMENU_DISAVOWED: return "disavowed_mainmenu";
  case MAINMENU_DEMOGAMEOVER: return "demogameover_mainmenu";
  case MAINMENU_WAREZGAMEOVER: return "warezgameover_mainmenu";
  case MAINMENU_REVELATIONWON: return "revelationwon_mainmenu";
  case MAINMENU_REVELATIONLOST: return "revelationlost_mainmenu";
  default: return nullptr;
  }
}

void HDEndScreens::UpdateGameOver() {
  const char *captionName = EndingCaption(activeScreen);
  if (!captionName)
    return;
  const std::string caption = LegacyCaption(captionName);
  const bool messageReady = HasCaption(caption);
  SetVisible(gameOver.Find("messageScroll"), messageReady);
  if (messageReady && activeScreen != MAINMENU_DISAVOWED)
    SetEndingText(gameOver, caption);
  const char *button = ReturnButton();
  const bool ready = button && LegacyHasButton(button);
  SetVisible(gameOver.Find("btnBack"), ready);
  if (ready != returnReady) {
    returnReady = ready;
    FocusBack();
  }
}

void HDEndScreens::UpdateConnectionLost() {
  // Legacy Update owns both >4-second stages, including its wall-clock
  // rounding. Reusing the actual controls also preserves re-entry timing.
  SetVisible(connectionLost.Find("txtCaption"),
             HasCaption(LegacyCaption("connectionlost")));
  const bool ready = LegacyHasButton("connectionlost_mainmenu");
  SetVisible(connectionLost.Find("btnBack"), ready);
  if (ready != returnReady) {
    returnReady = ready;
    FocusBack();
  }
}

void HDEndScreens::UpdateLoadError(bool failedLoad, MainMenuScreen *screen) {
  // Every actual Eclipse box belongs to the manager's universal message-box
  // mirror, including precise save-version/corruption errors. Only a failed
  // App::LoadGame file open returns to Login without creating such a box.
  if (observedScreen != MAINMENU_LOGIN || LegacyHasButton("msgbox_background")) {
    if (popupKind == PopupKind::LoadError) {
      popup.Hide();
      popupKind = PopupKind::None;
      errorMenu = nullptr;
    }
    return;
  }
  if (popupKind == PopupKind::LoadError) {
    if (errorMenu == screen)
      return;
    popup.Hide();
    popupKind = PopupKind::None;
    errorMenu = nullptr;
  }
  if (!failedLoad)
    return;
  if (popup.ShowInfo("Error", "Failed to load user profile",
                     [this] { ClosePopup(); })) {
    popupKind = PopupKind::LoadError;
    errorMenu = screen;
  }
}

void HDEndScreens::ClosePopup() {
  popupKind = PopupKind::None;
  errorMenu = nullptr;
  FocusBack();
}

bool HDEndScreens::Back() {
  if (!activeView || !activeView->IsVisible() || popup.IsVisible() || !app ||
      app->GetMainMenu()->InScreen() != activeScreen || !returnReady)
    return false;
  const char *button = ReturnButton();
  if (!button)
    return false;
  // Resolve immediately before the callback. It deletes its legacy screen.
  // Hidden HD trees are retained by HDLayoutView until event dispatch returns.
  if (!LegacyHasButton(button)) {
    if (activeScreen != MAINMENU_OBITUARY && activeScreen != MAINMENU_THETEAM)
      return false;
    app->GetMainMenu()->RunScreen(MAINMENU_LOGIN);
  } else {
    LegacyClick(button);
  }
  HideScreen();
  return true;
}

bool HDEndScreens::ShowTeam() {
#ifdef DEMOGAME
  return false;
#else
  if (activeScreen != MAINMENU_OBITUARY || !activeView ||
      !activeView->IsVisible() || popup.IsVisible() || !app ||
      app->GetMainMenu()->InScreen() != MAINMENU_OBITUARY)
    return false;
  if (LegacyHasButton("pi"))
    LegacyClick("pi");
  else
    app->GetMainMenu()->RunScreen(MAINMENU_THETEAM);
  HideScreen();
  return true;
#endif
}

void HDEndScreens::ShowAward(unsigned bit) {
  if (bit >= 12 || activeScreen != MAINMENU_OBITUARY || !game || !activeView ||
      !activeView->IsVisible() || popup.IsVisible() || !app ||
      app->GetMainMenu()->InScreen() != MAINMENU_OBITUARY)
    return;
  const unsigned bits = static_cast<unsigned>(
      game->GetObituary()->specialmissionscompleted);
  const std::string mission = PlotGenerator::SpecialMissionTitle(bit);
  const std::string body = bits & (1u << bit)
      ? "Completed Special Mission '" + mission + "'"
      : "Special Mission '" + mission + "' not completed";
  if (popup.ShowInfo("Special Awards", body, [this] { ClosePopup(); }))
    popupKind = PopupKind::Award;
}

bool HDEndScreens::ShowAwards() {
  if (activeScreen != MAINMENU_OBITUARY || !game || !activeView ||
      !activeView->IsVisible() || popup.IsVisible() || !app ||
      app->GetMainMenu()->InScreen() != MAINMENU_OBITUARY)
    return false;
  const unsigned bits = static_cast<unsigned>(
      game->GetObituary()->specialmissionscompleted);
  std::string body = "Named awards: " + std::to_string(CountBits(bits & 0xfffu)) +
                     "/12\nOther recorded flags: ";
  bool first = true;
  // There are only twelve authored icons. Never remap flags 12-15 onto those
  // icons or silently discard them. Include higher stored int bits as well.
  for (unsigned bit = 12; bit < std::numeric_limits<unsigned>::digits; ++bit) {
    if (!(bits & (1u << bit)))
      continue;
    if (!first)
      body += ',';
    body += std::to_string(bit);
    first = false;
  }
  if (first)
    body += "none";
  char mask[48];
  std::snprintf(mask, sizeof(mask), "\nAward flags: 0x%08X", bits);
  body += mask;
  if (!popup.ShowInfo("Special Awards", body, [this] { ClosePopup(); }))
    return false;
  popupKind = PopupKind::Award;
  return true;
}

bool HDEndScreens::Invoke(const std::string &command) {
  if (command == "Back")
    return Back();
  if (command == "PI")
    return ShowTeam();
  if (command == "Awards")
    return ShowAwards();
  return false;
}

bool HDEndScreens::HandleInput(const ALLEGRO_EVENT &event) {
  if (popup.HandleInput(event))
    return true;
  if (!activeView || !activeView->IsVisible())
    return false;
  if (event.type != ALLEGRO_EVENT_KEY_DOWN &&
      event.type != ALLEGRO_EVENT_KEY_UP && event.type != ALLEGRO_EVENT_KEY_CHAR)
    return false;
  if (event.type == ALLEGRO_EVENT_KEY_CHAR) {
    const int key = event.keyboard.keycode;
    if (key == ALLEGRO_KEY_ESCAPE || key == ALLEGRO_KEY_BACKSPACE) {
      Back();
      return true;
    }
    if (key == ALLEGRO_KEY_P && activeScreen == MAINMENU_OBITUARY &&
        !(event.keyboard.modifiers & (ALLEGRO_KEYMOD_CTRL | ALLEGRO_KEYMOD_ALT))) {
      ShowTeam();
      return true;
    }
    if ((key == ALLEGRO_KEY_ENTER || key == ALLEGRO_KEY_PAD_ENTER ||
         key == ALLEGRO_KEY_SPACE) && activeScreen == MAINMENU_OBITUARY &&
        ownerRoot.GetFocus() == obituary.Find("contents/txtAwardsVal")) {
      ShowAwards();
      return true;
    }
    if (activeView == &gameOver &&
        (key == ALLEGRO_KEY_UP || key == ALLEGRO_KEY_DOWN ||
         key == ALLEGRO_KEY_PGUP || key == ALLEGRO_KEY_PGDN)) {
      if (auto *scroll = gameOver.FindAs<UIScrollableList>("messageScroll")) {
        const float amount = key == ALLEGRO_KEY_PGUP || key == ALLEGRO_KEY_PGDN
            ? scroll->h : scroll->wheelStep;
        const bool up = key == ALLEGRO_KEY_UP || key == ALLEGRO_KEY_PGUP;
        scroll->SetScroll(0, scroll->scrollY + (up ? -amount : amount));
      }
      return true;
    }
  }
  // Dispatch once, then consume even unbound keys so legacy login/gameplay
  // shortcuts cannot fire behind these full-screen presentation trees.
  ownerRoot.DispatchEvent(event);
  return true;
}

} // namespace HDUI
