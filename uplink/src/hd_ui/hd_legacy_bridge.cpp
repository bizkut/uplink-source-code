#include "hd_legacy_bridge.h"
#include "hd_layout_parser.h"
#include "eclipse.h"
#include "app/opengl_interface.h"
#include <algorithm>
#include <cstdio>
#include <exception>
#include <utility>

namespace HDUI {
namespace {
void ClearCallbacks(UIObject *object) {
  if (!object)
    return;
  object->onClickCallback = nullptr;
  object->onHoverCallback = nullptr;
  if (auto *field = dynamic_cast<UIButtonTextField *>(object))
    field->onEnterCallback = nullptr;
  for (const auto &child : object->children)
    ClearCallbacks(child.get());
}

void ReleaseInactiveInput(UIObject *object) {
  UIObject *root = object->GetRoot();
  UIObject *captured = root->GetPointerCapture();
  if (captured && captured->IsDescendantOf(object))
    root->ReleasePointer();
  UIObject *focused = root->GetFocus();
  if (focused && focused->IsDescendantOf(object))
    root->SetFocus(nullptr);
}

UITextPoint *Caption(UIObject *object) {
  if (!object)
    return nullptr;
  if (auto *text = dynamic_cast<UITextPoint *>(object))
    return text;
  return dynamic_cast<UITextPoint *>(object->FindChildRecursive("txtCaption"));
}

void LegacyError(const char *operation, const char *name, const char *detail) {
  std::fprintf(stderr, "HD_UI: Cannot %s Eclipse button '%s': %s\n", operation,
               name ? name : "", detail);
}

class DraggableTitle : public UIInteractive {
public:
  DraggableTitle() : pointerX(0), pointerY(0), startX(0), startY(0) {}

  void OnPointerDown(float px, float py, int button) override {
    if (button != 1 || !parent || !IsEnabled())
      return;
    UIObject::OnPointerDown(px, py, button);
    pointerX = px;
    pointerY = py;
    startX = parent->x;
    startY = parent->y;
    for (UIObject *window = parent; window; window = window->parent)
      window->BringToFront();
  }

  void OnPointerMove(float px, float py) override {
    if (!isPressed || !parent || !parent->parent)
      return;
    UIObject *window = parent;
    const Geometry container = window->parent->GetGeometry();
    if (container.sx <= 0 || container.sy <= 0)
      return;
    window->x = startX + (px - pointerX) / container.sx;
    window->y = startY + (py - pointerY) / container.sy;
    const Geometry bounds = window->GetRoot()->GetGeometry();
    const Geometry geometry = window->GetGeometry();
    // GOLD's owner is a 1920x1080 root, including when its display is scaled.
    const float width = 1920.0f * bounds.sx;
    const float height = 1080.0f * bounds.sy;
    const float maxX = bounds.x + std::max(0.0f, width - geometry.w);
    const float maxY = bounds.y + std::max(0.0f, height - geometry.h);
    const float x = std::max(bounds.x, std::min(geometry.x, maxX));
    const float y = std::max(bounds.y, std::min(geometry.y, maxY));
    window->x += (x - geometry.x) / container.sx;
    window->y += (y - geometry.y) / container.sy;
  }

private:
  float pointerX, pointerY, startX, startY;
};
} // namespace

struct HDLayoutView::State {
  class Host : public UIContainer {
  public:
    explicit Host(const std::shared_ptr<State> &value)
        : state(value), captureInput(false) {
      w = 1920;
      h = 1080;
      visible = false;
    }
    ~Host() override {
      // The owner may clear its children before the controller goes away.
      // Invalidate the only borrowed tree link before destroying any widgets.
      if (auto live = state.lock())
        if (live->host == this)
          live->host = nullptr;
    }
    UIObject *Layout() const {
      return children.empty() ? nullptr : children.front().get();
    }
    bool OnMouseWheel(float, float) override { return captureInput; }
    bool OnTextInput(int) override { return captureInput; }
    bool OnKey(int, unsigned, bool) override { return captureInput; }
    std::weak_ptr<State> state;
    bool captureInput;
  };
  Host *host = nullptr;
};

HDLayoutView::HDLayoutView(UIObject &root, const std::string &name)
    : ownerRoot(root), layoutName(name), state(std::make_shared<State>()),
      captureInput(false) {}

HDLayoutView::~HDLayoutView() noexcept {
  ClearCallbacks(Root());
  try {
    Hide();
  } catch (const std::exception &error) {
    std::fprintf(stderr, "HD_UI: Cannot detach layout '%s': %s\n",
                 layoutName.c_str(), error.what());
  } catch (...) {
    std::fprintf(stderr, "HD_UI: Cannot detach layout '%s': unexpected exception\n",
                 layoutName.c_str());
  }
  // A throwing focus-loss callback must not leave an attached tree calling a
  // dead controller. Destruction clears the root's borrowed input links without
  // calling that focus callback again. Normal Hide always uses DetachChild.
  State::Host *host = state->host;
  if (host && host->parent) {
    auto &siblings = host->parent->children;
    const auto found = std::find_if(siblings.begin(), siblings.end(),
                                    [host](const std::unique_ptr<UIObject> &child) {
                                      return child.get() == host;
                                    });
    if (found != siblings.end()) {
      std::unique_ptr<UIObject> retired = std::move(*found);
      siblings.erase(found);
    }
  }
}

bool HDLayoutView::Load() {
  if (state->host) {
    if (Root())
      return true;
    std::fprintf(stderr, "HD_UI: Layout '%s' has lost its XML tree\n",
                 layoutName.c_str());
    return false;
  }
  if (layoutName.empty()) {
    std::fprintf(stderr, "HD_UI: Cannot load an unnamed layout\n");
    return false;
  }
  std::unique_ptr<UIObject> layout =
      LayoutManager::GetInstance().TakeLayout(layoutName);
  if (!layout) {
    std::fprintf(stderr, "HD_UI: Cannot load view layout '%s'\n",
                 layoutName.c_str());
    return false;
  }
  if (layoutName.compare(0, 15, "HUD/interfaces/") == 0) {
    // Eclipse still owns the live remote screen model and callbacks.  Cover
    // only its workspace while the GOLD view is attached; leave the always-on
    // top bar, task bar, and compact world map exposed.
    std::unique_ptr<UIRectangle> backdrop(new UIRectangle());
    backdrop->name = "legacyWorkspaceBackdrop";
    backdrop->x = 0;
    backdrop->y = 26;
    backdrop->w = 1360;
    backdrop->h = 992;
    backdrop->fillColor = "000000";
    backdrop->fillAlpha = 1.0f;
    backdrop->interactive = true;
    layout->children.insert(layout->children.begin(), std::move(backdrop));
    layout->children.front()->parent = layout.get();
  }
  std::unique_ptr<State::Host> host(new State::Host(state));
  host->name = "HDLayoutView:" + layoutName;
  host->captureInput = captureInput;
  host->interactive = captureInput;
  host->focusable = captureInput;
  host->AddChild(std::move(layout));
  state->host = host.get();
  detached = std::move(host);
  return true;
}

bool HDLayoutView::Show() {
  if (!Load())
    return false;
  State::Host *host = state->host;
  host->visible = true;
  Root()->visible = true;
  if (!host->parent) {
    if (!detached || detached.get() != host) {
      std::fprintf(stderr, "HD_UI: Layout '%s' has no retained owner\n",
                   layoutName.c_str());
      host->visible = false;
      return false;
    }
    ownerRoot.AddChild(std::move(detached));
  }
  host->BringToFront();
  return true;
}

void HDLayoutView::Hide() {
  State::Host *host = state->host;
  if (!host)
    return;
  host->visible = false;
  if (!host->parent)
    return;
  std::unique_ptr<UIObject> tree = host->parent->DetachChild(host);
  if (!tree) {
    std::fprintf(stderr, "HD_UI: Cannot detach layout '%s' from its owner\n",
                 layoutName.c_str());
    return;
  }
  detached = std::move(tree);
}

bool HDLayoutView::IsVisible() const {
  UIObject *layout = Root();
  return IsAttached() && layout && layout->IsVisible();
}

bool HDLayoutView::IsAttached() const {
  return state->host && state->host->parent;
}

void HDLayoutView::BringToFront() {
  if (state->host)
    state->host->BringToFront();
}

void HDLayoutView::SetCaptureInput(bool capture) {
  captureInput = capture;
  if (State::Host *host = state->host) {
    host->captureInput = capture;
    host->interactive = capture;
    host->focusable = capture;
    if (!capture && host->GetFocus() == host)
      host->SetFocus(nullptr);
  }
}

bool HDLayoutView::CapturesInput() const { return captureInput; }

UIObject *HDLayoutView::Root() const {
  return state->host ? state->host->Layout() : nullptr;
}

UIObject *HDLayoutView::Find(const std::string &path, bool required) const {
  UIObject *layout = Root();
  UIObject *object = layout ? layout->FindChildRecursive(path) : nullptr;
  if (!object && required)
    ReportChildError(path, "is missing");
  return object;
}

void HDLayoutView::ReportChildError(const std::string &path,
                                    const char *detail) const {
  std::fprintf(stderr, "HD_UI: Layout '%s' child '%s' %s\n", layoutName.c_str(),
               path.c_str(), detail);
}

Button *LegacyFindButton(const char *name) {
  return name && *name ? EclGetButton(const_cast<char *>(name)) : nullptr;
}

bool LegacyHasButton(const char *name) { return LegacyFindButton(name) != nullptr; }

bool LegacyClick(const char *name) {
  Button *button = LegacyFindButton(name);
  if (!button) {
    LegacyError("click", name, "button does not exist");
    return false;
  }
  button->MouseUp();
  return true;
}

bool LegacyClickFirst(std::initializer_list<const char *> names) {
  for (const char *name : names) {
    if (Button *button = LegacyFindButton(name)) {
      button->MouseUp();
      return true;
    }
  }
  std::fprintf(stderr, "HD_UI: Cannot click Eclipse button: no candidate exists\n");
  return false;
}

std::string LegacyCaption(const char *name) {
  Button *button = LegacyFindButton(name);
  return button && button->caption ? button->caption : "";
}

bool LegacySetCaption(const char *name, const std::string &caption) {
  Button *button = LegacyFindButton(name);
  if (!button) {
    LegacyError("set caption on", name, "button does not exist");
    return false;
  }
  if (!button->caption || caption != button->caption) {
    button->SetCaption(const_cast<char *>(caption.c_str()));
    button->Dirty();
  }
  return true;
}

bool SetText(UIObject *object, const std::string &text) {
  if (auto *field = dynamic_cast<UIButtonTextField *>(object)) {
    if (field->inputText != text)
      field->SetText(text);
    return true;
  }
  if (UITextPoint *caption = Caption(object)) {
    if (caption->text != text)
      caption->text = text;
    return true;
  }
  std::fprintf(stderr, "HD_UI: Cannot set text on HD control '%s': no text widget\n",
               object ? object->name.c_str() : "");
  return false;
}

std::string GetText(UIObject *object) {
  if (auto *field = dynamic_cast<UIButtonTextField *>(object))
    return field->inputText;
  UITextPoint *caption = Caption(object);
  return caption ? caption->text : std::string();
}

void SetVisible(UIObject *object, bool visible) {
  if (!object)
    return;
  object->visible = visible;
  if (!visible)
    ReleaseInactiveInput(object);
}

void SetEnabled(UIObject *object, bool enabled) {
  if (!object)
    return;
  object->enabled = enabled;
  if (!enabled)
    ReleaseInactiveInput(object);
}

void SetTooltip(UIObject *object, const std::string &text,
                const std::string &hotkeyHint) {
  if (!object)
    return;
  object->tooltipText = text;
  object->tooltipHotkeyHint = hotkeyHint;
  object->tooltipLegacyButton.clear();
}

void BindLegacyTooltip(UIObject *object, const std::string &buttonName,
                       const std::string &hotkeyHint) {
  if (!object)
    return;
  object->tooltipText.clear();
  object->tooltipHotkeyHint = hotkeyHint;
  object->tooltipLegacyButton = buttonName;
}

bool SyncLegacyInput(const UIButtonTextField *field, const char *editableName) {
  if (!field) {
    LegacyError("synchronize input into", editableName, "HD field is missing");
    return false;
  }
  if (!LegacyHasButton(editableName)) {
    LegacyError("synchronize input into", editableName, "button does not exist");
    return false;
  }
  if (!EclIsButtonEditable(const_cast<char *>(editableName))) {
    LegacyError("synchronize input into", editableName, "button is not editable");
    return false;
  }
  return LegacySetCaption(editableName, field->inputText);
}

bool SyncLegacyInputAndClick(const UIButtonTextField *field,
                             const char *editableName, const char *actionName) {
  if (!LegacyHasButton(actionName)) {
    LegacyError("click", actionName, "button does not exist");
    return false;
  }
  return SyncLegacyInput(field, editableName) && LegacyClick(actionName);
}

HDRepeatLayoutFactory::HDRepeatLayoutFactory(const std::string &name,
                                             const std::string &path)
    : layoutName(name), childPath(path) {}

std::unique_ptr<UIObject> HDRepeatLayoutFactory::Create() {
  error.clear();
  if (layoutName.empty()) {
    error = "Cannot parse an unnamed repeated layout";
    std::fprintf(stderr, "HD_UI: %s\n", error.c_str());
    return nullptr;
  }
  std::unique_ptr<UIObject> layout =
      LayoutManager::GetInstance().TakeLayout(layoutName);
  if (!layout) {
    // LayoutManager prints the parser's precise file/XML error, not a partial
    // tree. Retain the template context for callers as well.
    error = "Cannot parse repeated layout '" + layoutName + "'";
    std::fprintf(stderr, "HD_UI: %s\n", error.c_str());
    return nullptr;
  }
  if (childPath.empty())
    return layout;
  UIObject *row = layout->FindChildRecursive(childPath);
  if (!row) {
    error = "Repeated layout '" + layoutName + "' is missing template '" +
            childPath + "'";
    std::fprintf(stderr, "HD_UI: %s\n", error.c_str());
    return nullptr;
  }
  if (row == layout.get())
    return layout;
  std::unique_ptr<UIObject> result = row->parent->DetachChild(row);
  if (!result) {
    error = "Cannot detach template '" + childPath + "' from repeated layout '" +
            layoutName + "'";
    std::fprintf(stderr, "HD_UI: %s\n", error.c_str());
  }
  return result;
}

HDTooltipController::HDTooltipController(UIObject &root, float delay)
    : ownerRoot(root), view(root, "Tooltip"), hoverRevision(0), elapsed(0),
      delay(std::max(0.0f, delay)), preparedWidth(-1) {}

bool HDTooltipController::Prepare() {
  if (!view.Load())
    return false;
  UIObject *frame = view.Root()->FindChild("Tooltip");
  UITextPoint *caption = view.FindAs<UITextPoint>("txtCaption");
  if (!frame || !caption || !view.Find("partL") || !view.Find("partM") ||
      !view.Find("partR"))
    return false;
  // Preserve the authored font, colors and frame, but wrap supplied captions
  // rather than clipping long descriptions at Tooltip.xml's sample width.
  if (!dynamic_cast<UITextParagraph *>(caption)) {
    std::unique_ptr<UITextParagraph> paragraph(new UITextParagraph());
    paragraph->name = caption->name;
    paragraph->x = caption->x;
    paragraph->y = caption->y;
    paragraph->font = caption->font;
    paragraph->fontSize = caption->fontSize;
    paragraph->leading = caption->leading;
    paragraph->assetBasePath = caption->assetBasePath;
    paragraph->fillColor = caption->fillColor;
    paragraph->align = caption->align;
    UIObject *parent = caption->parent;
    parent->DetachChild(caption);
    parent->AddChild(std::move(paragraph));
  }
  // Disabling the complete XML subtree prevents even future authored controls
  // from becoming hit targets. Drawing is unaffected and the host never captures.
  view.Root()->enabled = false;
  return true;
}

void HDTooltipController::Hide() {
  view.Hide();
  hoverRevision = 0;
  elapsed = 0;
  text.clear();
  hotkeyHint.clear();
  captionText.clear();
}

void HDTooltipController::Update(float dt) {
  UIObject *target = ownerRoot.GetHovered();
  while (target && target->tooltipText.empty() &&
         target->tooltipLegacyButton.empty() && target->tooltipHotkeyHint.empty())
    target = target->parent;
  if (!target || ownerRoot.GetPointerCapture()) {
    Hide();
    return;
  }
  const char *supplied = target->tooltipText.c_str();
  if (!target->tooltipLegacyButton.empty()) {
    Button *button = LegacyFindButton(target->tooltipLegacyButton.c_str());
    supplied = button && button->tooltip ? button->tooltip : "";
  }
  const std::string &hint = target->tooltipHotkeyHint;
  const Geometry anchor = target->GetGeometry();
  const unsigned long long revision = ownerRoot.GetHoverRevision();
  // Do not use target after preparing/showing a layout. No control or legacy
  // button address survives this frame, even when an allocator reuses it.
  if (!supplied[0] && hint.empty()) {
    Hide();
    return;
  }
  if (revision != hoverRevision || supplied != text || hint != hotkeyHint) {
    view.Hide();
    hoverRevision = revision;
    elapsed = 0;
    text = supplied;
    hotkeyHint = hint;
    captionText = text;
    if (!hotkeyHint.empty()) {
      if (!captionText.empty())
        captionText += "  ";
      captionText += "[";
      captionText += hotkeyHint;
      captionText += "]";
    }
  }
  elapsed = std::min(delay, elapsed + std::max(0.0f, dt));
  if (elapsed < delay || !Prepare())
    return;
  UIObject *frame = view.Root()->FindChild("Tooltip");
  auto *caption = view.FindAs<UITextParagraph>("txtCaption");
  // The detached host has the same 1920x1080 coordinate space as the owner.
  const Geometry bounds = ownerRoot.GetGeometry();
  const Geometry geometry = frame->GetGeometry();
  const float rootWidth = bounds.w > 0 ? bounds.w : 1920 * bounds.sx;
  const float rootHeight = bounds.h > 0 ? bounds.h : 1080 * bounds.sy;
  const float maximumWidth = geometry.sx > 0 ?
      std::max(38.0f, std::min(640.0f, (rootWidth - 16 * bounds.sx) / geometry.sx)) : 640;
  if (caption->text != captionText || preparedWidth != maximumWidth) {
    caption->text = captionText;
    preparedWidth = maximumWidth;
    float measured = 0;
    for (size_t begin = 0; begin < captionText.size();) {
      const size_t end = captionText.find_first_of("\r\n", begin);
      measured = std::max(measured, caption->Measure(begin, end == std::string::npos ? captionText.size() : end));
      if (end == std::string::npos)
        break;
      begin = end + 1;
    }
    frame->w = std::min(maximumWidth, std::max(238.0f, measured + 36));
    caption->w = std::max(1.0f, frame->w - 36);
    caption->PrepareText();
    const float lineHeight = caption->GetFont() ?
        static_cast<float>(al_get_font_line_height(caption->GetFont())) : caption->fontSize;
    caption->h = lineHeight + std::max(0.0f, static_cast<float>(caption->lines.size()) - 1) *
        std::max(1.0f, lineHeight + caption->leading);
    frame->h = std::max(38.0f, caption->h + 8);
    UIObject *middle = view.Find("partM");
    middle->w = std::max(0.0f, frame->w - 38);
    middle->h = frame->h;
    UIObject *left = view.Find("partL");
    UIObject *right = view.Find("partR");
    right->x = frame->w - 19;
    left->scaleY = left->h > 0 ? frame->h / left->h : 1;
    right->scaleY = right->h > 0 ? frame->h / right->h : 1;
  }
  const Geometry sized = frame->GetGeometry();
  float px = anchor.x + (anchor.w - sized.w) * 0.5f;
  float py = anchor.y + anchor.h + 8 * bounds.sy;
  if (py + sized.h > bounds.y + rootHeight)
    py = anchor.y - sized.h - 8 * bounds.sy;
  px = std::max(bounds.x, std::min(px, bounds.x + std::max(0.0f, rootWidth - sized.w)));
  py = std::max(bounds.y, std::min(py, bounds.y + std::max(0.0f, rootHeight - sized.h)));
  const Geometry inherited = frame->parent->GetGeometry();
  if (inherited.sx > 0)
    frame->x += (px - sized.x) / inherited.sx;
  if (inherited.sy > 0)
    frame->y += (py - sized.y) / inherited.sy;
  view.Show();
}

HDPopupController::HDPopupController(UIObject &ownerRoot)
    : view(ownerRoot, "PopUp"), confirmation(false), finishing(false) {
  view.SetCaptureInput(true);
}

HDPopupController::~HDPopupController() noexcept {
  // HDLayoutView clears widget callbacks before detaching/destruction.
  acceptCallback = nullptr;
  cancelCallback = nullptr;
}

bool HDPopupController::Prepare() {
  if (!view.Load())
    return false;
  UIObject *dialogue = view.Find("dialogueBox");
  UITextPoint *title = view.FindAs<UITextPoint>("dialogueBox/txtTitle");
  UITextParagraph *contents = view.FindAs<UITextParagraph>("txtContents");
  UIButtonStandard *ok = view.FindAs<UIButtonStandard>("dialogueBox/btnOK");
  UIButtonStandard *cancel =
      view.FindAs<UIButtonStandard>("dialogueBox/btnCancel");
  if (!dialogue || !title || !contents || !ok || !cancel) {
    Hide();
    return false;
  }
  UIObject *existing = view.Find("dialogueBox/popupBodyViewport", false);
  UIScrollableList *viewport = dynamic_cast<UIScrollableList *>(existing);
  if (existing && (!viewport || contents->parent != viewport)) {
    std::fprintf(stderr, "HD_UI: Popup body viewport has an invalid widget tree\n");
    Hide();
    return false;
  }
  if (!viewport) {
    std::unique_ptr<UIScrollableList> scroll(new UIScrollableList());
    scroll->name = "popupBodyViewport";
    scroll->x = contents->x;
    scroll->y = contents->y;
    scroll->w = contents->w;
    scroll->h = contents->h;
    std::unique_ptr<UIObject> paragraph = contents->parent->DetachChild(contents);
    if (!paragraph) {
      std::fprintf(stderr, "HD_UI: Cannot detach popup body for its viewport\n");
      Hide();
      return false;
    }
    paragraph->x = 0;
    paragraph->y = 0;
    scroll->AddChild(std::move(paragraph));
    dialogue->AddChild(std::move(scroll));
  }
  ok->onClickCallback = [this](UIObject *) { Accept(); };
  cancel->onClickCallback = [this](UIObject *) { Cancel(); };
  return true;
}

bool HDPopupController::ShowInfo(const std::string &title, const std::string &body,
                                 Callback onClose) {
  return Show(title, body, false, std::move(onClose), Callback());
}

bool HDPopupController::ShowConfirm(const std::string &title,
                                    const std::string &body, Callback onConfirm,
                                    Callback onCancel) {
  return Show(title, body, true, std::move(onConfirm), std::move(onCancel));
}

bool HDPopupController::SetMessage(const std::string &title,
                                  const std::string &body) {
  UITextPoint *heading = view.FindAs<UITextPoint>("dialogueBox/txtTitle");
  UITextParagraph *contents = view.FindAs<UITextParagraph>("txtContents");
  UIScrollableList *viewport =
      view.FindAs<UIScrollableList>("dialogueBox/popupBodyViewport");
  if (!heading || !contents || !viewport ||
      !SetText(heading, title) || !SetText(contents, body)) {
    Hide();
    return false;
  }
  contents->PrepareText();
  ALLEGRO_FONT *font = contents->GetFont();
  const float lineHeight = font ? static_cast<float>(al_get_font_line_height(font))
                                : static_cast<float>(contents->fontSize);
  const float advance = std::max(1.0f, lineHeight + contents->leading);
  contents->h = std::max(viewport->h,
                         advance * static_cast<float>(contents->lines.size()));
  viewport->ClampScroll();
  return true;
}

bool HDPopupController::Show(const std::string &title, const std::string &body,
                             bool confirm, Callback onConfirm,
                             Callback onCancel) {
  if (!Prepare())
    return false;
  // Preserve supplied text exactly: no uppercasing, truncation or placeholder.
  if (!SetMessage(title, body))
    return false;
  view.FindAs<UIScrollableList>("dialogueBox/popupBodyViewport")->SetScroll(0, 0);
  confirmation = confirm;
  SetVisible(view.Find("dialogueBox/btnCancel"), confirm);
  SetEnabled(view.Find("dialogueBox/btnCancel"), confirm);
  SetEnabled(view.Find("dialogueBox/btnOK"), true);
  acceptCallback = std::move(onConfirm);
  cancelCallback = std::move(onCancel);
  if (!view.Show()) {
    Hide();
    return false;
  }
  // A popup opened during an existing drag must not deliver its first mouse
  // release to the formerly captured background control.
  view.Root()->ResetInput();
  Focus();
  return true;
}

void HDPopupController::Hide() {
  acceptCallback = nullptr;
  cancelCallback = nullptr;
  view.Hide();
}

bool HDPopupController::IsVisible() const { return view.IsVisible(); }

void HDPopupController::Focus() {
  if (!IsVisible())
    return;
  view.BringToFront();
  UIObject *layout = view.Root();
  UIObject *focused = layout->GetFocus();
  if (focused && focused->IsDescendantOf(layout))
    return;
  UIObject *ok = view.Find("dialogueBox/btnOK");
  if (ok)
    ok->SetFocus(ok);
}

void HDPopupController::Accept() { Finish(true); }

void HDPopupController::Cancel() { Finish(!confirmation); }

void HDPopupController::Finish(bool accepted) {
  if (finishing || !IsVisible())
    return;
  finishing = true;
  Callback callback = std::move(accepted ? acceptCallback : cancelCallback);
  Hide();
  finishing = false;
  if (callback)
    callback();
}

bool HDPopupController::HandleKey(int key, unsigned modifiers, bool down) {
  if (!IsVisible())
    return false;
  UIObject *layout = view.Root();
  UIObject *focused = layout->GetFocus();
  if (!focused || !focused->IsDescendantOf(layout))
    Focus();
  if (down && (key == ALLEGRO_KEY_ENTER || key == ALLEGRO_KEY_PAD_ENTER)) {
    UIObject *cancel = view.Find("dialogueBox/btnCancel");
    if (confirmation && cancel && cancel->GetFocus() == cancel)
      Cancel();
    else
      Accept();
  } else if (down && key == ALLEGRO_KEY_ESCAPE) {
    Cancel();
  } else if (down && key == ALLEGRO_KEY_TAB) {
    UIObject *ok = view.Find("dialogueBox/btnOK");
    UIObject *cancel = view.Find("dialogueBox/btnCancel");
    if (ok) {
      UIObject *next = confirmation && cancel && ok->GetFocus() == ok ? cancel : ok;
      ok->SetFocus(next);
    }
  } else if (down && (key == ALLEGRO_KEY_UP || key == ALLEGRO_KEY_DOWN ||
                      key == ALLEGRO_KEY_PGUP || key == ALLEGRO_KEY_PGDN ||
                      key == ALLEGRO_KEY_HOME || key == ALLEGRO_KEY_END)) {
    UIScrollableList *viewport =
        view.FindAs<UIScrollableList>("dialogueBox/popupBodyViewport");
    if (viewport) {
      float position = viewport->scrollY;
      if (key == ALLEGRO_KEY_UP || key == ALLEGRO_KEY_DOWN)
        position += key == ALLEGRO_KEY_UP ? -viewport->wheelStep : viewport->wheelStep;
      else if (key == ALLEGRO_KEY_PGUP || key == ALLEGRO_KEY_PGDN)
        position += key == ALLEGRO_KEY_PGUP ? -viewport->h : viewport->h;
      else if (key == ALLEGRO_KEY_HOME)
        position = 0;
      else if (UIObject *contents = viewport->FindChild("txtContents"))
        position = contents->h;
      viewport->SetScroll(0, position);
    }
  } else {
    // A button's Space activation remains the real HD button callback; other
    // keys are consumed even when the focused widget does not handle them.
    layout->DispatchKey(key, modifiers, down);
  }
  return true;
}

bool HDPopupController::HandleInput(const ALLEGRO_EVENT &event) {
  if (!IsVisible())
    return false;
  view.BringToFront();
  switch (event.type) {
  case ALLEGRO_EVENT_KEY_DOWN:
    // The activation arrives as KEY_CHAR. Swallow its precursor so Eclipse
    // cannot perform a second action before the popup handles the character.
    return true;
  case ALLEGRO_EVENT_KEY_CHAR:
    HandleKey(event.keyboard.keycode, event.keyboard.modifiers, true);
    return true;
  case ALLEGRO_EVENT_KEY_UP:
    HandleKey(event.keyboard.keycode, event.keyboard.modifiers, false);
    return true;
  case ALLEGRO_EVENT_MOUSE_AXES:
  case ALLEGRO_EVENT_MOUSE_BUTTON_DOWN:
  case ALLEGRO_EVENT_MOUSE_BUTTON_UP:
    view.Root()->DispatchEvent(event);
    return true;
  case ALLEGRO_EVENT_DISPLAY_SWITCH_OUT:
  case ALLEGRO_EVENT_MOUSE_LEAVE_DISPLAY:
    view.Root()->DispatchEvent(event);
    return false;
  default:
    return false;
  }
}

HDLegacyMessageBoxMirror::HDLegacyMessageBoxMirror(UIObject &ownerRoot)
    : popup(ownerRoot), backgroundIdentity(0), titleIdentity(0), textIdentity(0),
      acceptIdentity(0), cancelIdentity(0), acceptName(nullptr),
      cancelName(nullptr) {}

void HDLegacyMessageBoxMirror::Update() {
  if (!isvisible_msgbox()) {
    Hide();
    return;
  }
  Button *background = LegacyFindButton("msgbox_background");
  Button *heading = LegacyFindButton("msgbox_title");
  Button *contents = LegacyFindButton("msgbox_text");
  Button *close = LegacyFindButton("msgbox_close");
  Button *accept = close ? close : LegacyFindButton("msgbox_yes");
  Button *cancel = close ? nullptr : LegacyFindButton("msgbox_no");
  if (!background || !heading || !contents || !accept || (!close && !cancel)) {
    std::fprintf(stderr, "HD_UI: Cannot mirror an incomplete Eclipse message box\n");
    Hide();
    return;
  }
  const auto backgroundId = reinterpret_cast<std::uintptr_t>(background);
  const auto titleId = reinterpret_cast<std::uintptr_t>(heading);
  const auto textId = reinterpret_cast<std::uintptr_t>(contents);
  const auto acceptId = reinterpret_cast<std::uintptr_t>(accept);
  const auto cancelId = reinterpret_cast<std::uintptr_t>(cancel);
  const bool sameDialog = backgroundIdentity == backgroundId &&
                          titleIdentity == titleId && textIdentity == textId &&
                          acceptIdentity == acceptId && cancelIdentity == cancelId &&
                          (cancelName != nullptr) == (cancel != nullptr);
  const char *headingText = heading->caption ? heading->caption : "";
  const char *contentsText = contents->caption ? contents->caption : "";
  if (sameDialog && popup.IsVisible()) {
    // Caption animations change text without changing the live controls.
    // Do not Show again: that would reset focus, drag capture and scrolling.
    if (title != headingText || body != contentsText) {
      title = headingText;
      body = contentsText;
      if (!popup.SetMessage(title, body))
        Hide();
    }
    return;
  }
  popup.Hide();
  backgroundIdentity = backgroundId;
  titleIdentity = titleId;
  textIdentity = textId;
  acceptIdentity = acceptId;
  cancelIdentity = cancelId;
  acceptName = close ? "msgbox_close" : "msgbox_yes";
  cancelName = close ? nullptr : "msgbox_no";
  title = headingText;
  body = contentsText;
  const bool shown = close
      ? popup.ShowInfo(title, body, [this] { Invoke(true); })
      : popup.ShowConfirm(title, body, [this] { Invoke(true); },
                          [this] { Invoke(false); });
  if (!shown)
    Hide();
}

void HDLegacyMessageBoxMirror::Invoke(bool accepted) {
  const char *name = accepted ? acceptName : cancelName;
  const std::uintptr_t identity = accepted ? acceptIdentity : cancelIdentity;
  // An old HD callback must never act on a replacement dialog with the same
  // button names. Stored addresses are compared only, never dereferenced.
  const bool current = name && identity && isvisible_msgbox() &&
      reinterpret_cast<std::uintptr_t>(LegacyFindButton("msgbox_background")) ==
          backgroundIdentity &&
      reinterpret_cast<std::uintptr_t>(LegacyFindButton("msgbox_title")) ==
          titleIdentity &&
      reinterpret_cast<std::uintptr_t>(LegacyFindButton("msgbox_text")) ==
          textIdentity &&
      reinterpret_cast<std::uintptr_t>(LegacyFindButton(name)) == identity;
  Hide();
  if (!current) {
    std::fprintf(stderr, "HD_UI: Refusing a stale Eclipse message-box action\n");
    return;
  }
  // Hide clears all mirror state before the real callback can change screens
  // or destroy the controller. Nothing accesses this after the legacy call.
  LegacyClick(name);
}

bool HDLegacyMessageBoxMirror::HandleInput(const ALLEGRO_EVENT &event) {
  Update();
  return popup.HandleInput(event);
}

bool HDLegacyMessageBoxMirror::IsVisible() const { return popup.IsVisible(); }

void HDLegacyMessageBoxMirror::Hide() {
  popup.Hide();
  backgroundIdentity = titleIdentity = textIdentity = 0;
  acceptIdentity = cancelIdentity = 0;
  acceptName = cancelName = nullptr;
  title.clear();
  body.clear();
}

std::unique_ptr<UIObject> MakeDraggableTitle(float x, float y, float width,
                                           float height) {
  std::unique_ptr<DraggableTitle> title(new DraggableTitle());
  title->name = "HDDraggableTitle";
  title->x = x;
  title->y = y;
  title->w = std::max(0.0f, width);
  title->h = std::max(0.0f, height);
  return title;
}

} // namespace HDUI
