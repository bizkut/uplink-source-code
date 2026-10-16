#include "hd_intro.h"
#include "app/app.h"
#include "game/game.h"
#include "game/scriptlibrary.h"
#include "mainmenu/mainmenu.h"
#include "world/world.h"
#include "world/player.h"
#include "world/vlocation.h"
#include <algorithm>
#include <cstdio>

namespace HDUI {
namespace {
const float ConnectionSeconds = 4.5f;
const float ProgressSeconds = 10.0f;
}

HDIntro::HDIntro(UIObject &root)
    : HDScreen(root), popup(nullptr), steps{}, connectTo(nullptr),
      errorFocus(nullptr), input(nullptr), username(nullptr), password(nullptr),
      confirmation(nullptr), progressFill(nullptr), progressText(nullptr),
      step(-1), pendingStep(-1), progressPercent(-1), elapsed(0), stepAge(0) {
  layoutName = "IntroScreens";
}

HDIntro::~HDIntro() { Remove(); }

void HDIntro::Create() {
  HDScreen::Create();
  if (!IsVisible())
    return;
  std::unique_ptr<UIObject> modal = LayoutManager::GetInstance().TakeLayout("PopUp");
  if (!modal) {
    Remove();
    return;
  }
  popup = modal.get();
  popup->visible = false;
  popup->interactive = true;
  popup->zOrder = 1001;
  ownerRoot.AddChild(std::move(modal));
  layoutPtr->interactive = true;
  layoutPtr->SetZOrder(1000);

  for (int i = 0; i < 8; ++i) {
    char path[16];
    std::snprintf(path, sizeof(path), "step%d", i);
    steps[i] = FindElement(path);
    steps[i]->visible = false;
  }
  connectTo = FindElement("connectTo");
  connectTo->visible = false;
  input = dynamic_cast<UIButtonTextField *>(FindElement("step0/btnInput"));
  username = dynamic_cast<UIButtonTextField *>(FindElement("step4/btnUser"));
  password = dynamic_cast<UIButtonTextField *>(FindElement("step4/btnPass"));
  confirmation = dynamic_cast<UIButtonTextField *>(FindElement("step4/btnPass2"));
  progressFill = FindElement("step7/loadFill");
  progressText = dynamic_cast<UITextPoint *>(FindElement("step7/txtLoadProgress"));

  input->SetText("Uplink Public Access Server");
  input->onEnterCallback = [this](UIObject *) {
    Connect(0, 1, "Connecting to Uplink Public Access Server");
  };
  FindElement("step0/btnSubmit")->onClickCallback = input->onEnterCallback;
  BindStep("step1/btnOK", 1, 2);
  BindStep("step2/btnAbout", 2, 1);
  BindStep("step2/btnRegister", 2, 3);
  BindStep("step3/btnCancel", 3, 2);
  BindStep("step3/btnContinue", 3, 4);
  for (UIButtonTextField *field : {username, password, confirmation}) {
    field->maxLength = SIZE_AGENT_HANDLE - 1;
    field->isPassword = field != username;
    field->SetText("");
    // The final intro submits from Enter in any of the three fields.
    field->onEnterCallback = [this](UIObject *) { SubmitRegistration(); };
  }
  FindElement("step4/btnDone")->onClickCallback = username->onEnterCallback;
  const char *cities[] = {"btnLA", "btnCH", "btnNY", "btnLN",
                          "btnMW", "btnHK", "btnTK", "btnSY"};
  for (const char *city : cities)
    steps[5]->FindChild(city)->onClickCallback = [this](UIObject *) { SelectLocation(); };
  FindElement("step6/btnOK")->onClickCallback = [this](UIObject *) {
    Connect(6, 7, "Connecting to Gateway");
  };
  popup->FindChildRecursive("dialogueBox/btnCancel")->visible = false;
  popup->FindChildRecursive("dialogueBox/btnOK")->onClickCallback =
      [this](UIObject *) { CloseError(); };
  SetStep(0);
}

void HDIntro::Remove() {
  if (popup) {
    ownerRoot.DetachChild(popup);
    popup = nullptr;
  }
  HDScreen::Remove();
  for (UIObject *&container : steps)
    container = nullptr;
  connectTo = errorFocus = progressFill = nullptr;
  input = username = password = confirmation = nullptr;
  progressText = nullptr;
  step = pendingStep = -1;
  elapsed = stepAge = 0;
  progressPercent = -1;
}

void HDIntro::BindStep(const char *path, int from, int to) {
  FindElement(path)->onClickCallback = [this, from, to](UIObject *) {
    if (step == from && pendingStep < 0 && !popup->visible)
      SetStep(to);
  };
}

void HDIntro::SetStep(int next) {
  ownerRoot.SetFocus(nullptr);
  for (int i = 0; i < 8; ++i) {
    steps[i]->visible = i == next;
    steps[i]->alpha = 1;
  }
  connectTo->visible = false;
  step = next;
  pendingStep = -1;
  elapsed = stepAge = 0;
  static const char *focus[] = {"btnInput", "btnOK", "btnRegister", "btnContinue",
                                "btnUser", "btnLN", "btnOK", nullptr};
  if (focus[step])
    ownerRoot.SetFocus(steps[step]->FindChild(focus[step]));
  if (step == 7) {
    progressPercent = -1;
    UpdateProgress(0);
  }
}

void HDIntro::Connect(int from, int to, const char *caption) {
  if (step != from || pendingStep >= 0 || popup->visible)
    return;
  ownerRoot.SetFocus(nullptr);
  steps[step]->visible = false;
  connectTo->visible = true;
  connectTo->alpha = 1;
  static_cast<UITextPoint *>(connectTo->FindChild("txtCaption"))->text = caption;
  pendingStep = to;
  elapsed = 0;
}

bool HDIntro::HandleInput(const ALLEGRO_EVENT &event) {
  if (!IsVisible() || event.type != ALLEGRO_EVENT_KEY_CHAR ||
      event.keyboard.unichar < 32 ||
      (event.keyboard.modifiers & (ALLEGRO_KEYMOD_CTRL | ALLEGRO_KEYMOD_ALT)))
    return false;
  UIObject *focus = ownerRoot.GetFocus();
  if (focus != username && focus != password && focus != confirmation)
    return false;
  const UIButtonTextField *field = static_cast<UIButtonTextField *>(focus);
  // Model credentials are fixed-size byte strings; the core widget's length
  // limit counts Unicode characters, so also bound UTF-8 before insertion.
  const size_t limit = focus == username ? SIZE_AGENT_HANDLE - 1 : 63;
  return field->inputText.size() + al_utf8_width(event.keyboard.unichar) > limit;
}

void HDIntro::ShowError(const char *message, UIObject *field) {
  errorFocus = field;
  static_cast<UITextPoint *>(popup->FindChildRecursive("dialogueBox/txtTitle"))->text = "ERROR";
  static_cast<UITextPoint *>(popup->FindChildRecursive("dialogueBox/txtContents"))->text = message;
  layoutPtr->enabled = false;
  popup->visible = true;
  ownerRoot.SetFocus(popup->FindChildRecursive("dialogueBox/btnOK"));
}

void HDIntro::CloseError() {
  // Hide rather than detach: this is called by a borrowed modal control.
  popup->visible = false;
  layoutPtr->enabled = true;
  ownerRoot.SetFocus(errorFocus);
  errorFocus = nullptr;
}

void HDIntro::SubmitRegistration() {
  if (step != 4 || pendingStep >= 0 || popup->visible)
    return;
  const std::string &name = username->inputText;
  const std::string &pass = password->inputText;
  const std::string &again = confirmation->inputText;
  // GOLD sub_473590/sub_473420, including exact modal strings and ordering.
  if (name.empty()) {
    ShowError("Please enter a username.", username);
    return;
  }
  if (name.find_first_of(":/\\?.,\"<>|*") != std::string::npos) {
    ShowError("Username not valid. Please do not use any of the special characters:\n: / \\ ? . , \" < > | *", username);
    return;
  }
  if (name == "admin" || name == "readwrite" || name == "readonly") {
    ShowError("Username not valid. Please use a different username than 'admin', 'readwrite' or 'readonly'.", username);
    return;
  }
  if (pass.empty()) {
    ShowError("Please enter a password.", password);
    return;
  }
  if (again.empty()) {
    ShowError("Please retype your password.", confirmation);
    return;
  }
  if (pass != again) {
    ShowError("The passwords you have entered are not the same.", confirmation);
    return;
  }
  game->NewGame();
  ScriptLibrary::RegisterNewAgent(&username->inputText[0], &password->inputText[0]);
  SetStep(5);
}

void HDIntro::SelectLocation() {
  if (step != 5 || pendingStep >= 0 || popup->visible)
    return;
  // Final sub_475130/sub_4710E0 deliberately uses the same location for every city.
  game->GetWorld()->GetPlayer()->GetLocalHost()->SetPLocation(215, 215);
  SetStep(6);
}

void HDIntro::UpdateProgress(float dt) {
  elapsed += dt;
  const float progress = std::min(1.0f, elapsed / ProgressSeconds);
  progressFill->scaleX = progress;
  const int percent = static_cast<int>(progress * 100);
  if (progressPercent != percent) {
    char caption[16];
    std::snprintf(caption, sizeof(caption), "%d%%", percent);
    progressText->text = caption;
    progressPercent = percent;
  }
  if (elapsed >= ProgressSeconds)
    Finish();
}

void HDIntro::Finish() {
  step = -1;
  ScriptLibrary::CompleteFirstRun();
  // Commit the newly created profile before the player can quit from the
  // gateway welcome screen. The original autosave clock is not initialized.
  Player *player = game->GetWorld()->GetPlayer();
  app->SaveGame(player->handle);
  // HasGameSession relies on the legacy menu disappearing. The manager removes
  // our trees after this update unwinds, then exposes its interactive HUD.
  app->GetMainMenu()->Remove();
}

void HDIntro::Update(float dt) {
  if (!IsVisible() || step < 0)
    return;
  dt = std::max(0.0f, dt);
  if (pendingStep >= 0) {
    elapsed += dt;
    if (elapsed >= ConnectionSeconds) {
      const float remaining = elapsed - ConnectionSeconds;
      SetStep(pendingStep);
      if (step == 7)
        UpdateProgress(remaining);
    }
  } else if (step == 7) {
    UpdateProgress(dt);
  }
  // Only the active step fades in; inactive containers never hit-test or draw.
  if (step >= 0 && pendingStep < 0) {
    stepAge += dt;
    steps[step]->alpha = std::min(1.0f, 0.1f + stepAge / 0.5f);
  }
  // The manager updates the composed tree once, after this state-machine tick.
}

} // namespace HDUI
