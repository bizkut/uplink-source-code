#ifndef _included_hd_legacy_bridge_h
#define _included_hd_legacy_bridge_h

#include "hd_ui_object.h"
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>

class Button;

namespace HDUI {

// The owner must outlive the view. Show attaches an exclusively owned host to
// that owner; Hide detaches and retains it, including a currently running
// control callback. Never cache Find/Root results across tree destruction.
class HDLayoutView {
public:
  HDLayoutView(UIObject &ownerRoot, const std::string &layoutName);
  ~HDLayoutView() noexcept;
  HDLayoutView(const HDLayoutView &) = delete;
  HDLayoutView &operator=(const HDLayoutView &) = delete;

  bool Load();
  bool Show();
  void Hide();
  bool IsVisible() const;
  bool IsAttached() const;
  void BringToFront();
  // A transparent 1920x1080 host catches background pointer/wheel input.
  // Modal keyboard/hotkey routing is provided by HDPopupController below.
  void SetCaptureInput(bool capture);
  bool CapturesInput() const;

  UIObject *Root() const;
  UIObject *Find(const std::string &path, bool required = true) const;
  template <class T>
  T *FindAs(const std::string &path, bool required = true) const {
    UIObject *object = Find(path, required);
    T *typed = dynamic_cast<T *>(object);
    if (object && !typed && required)
      ReportChildError(path, "has the wrong widget type");
    return typed;
  }
  const std::string &LayoutName() const { return layoutName; }

private:
  struct State;
  void ReportChildError(const std::string &path, const char *detail) const;
  UIObject &ownerRoot;
  std::string layoutName;
  std::shared_ptr<State> state;
  std::unique_ptr<UIObject> detached;
  bool captureInput;
};

// Resolve on every use. A borrowed Button is valid only until the next legacy
// callback/screen change. Click calls MouseUp(), not Eclipse's visual click API,
// and never touches the Button after that call (it may have deleted itself).
Button *LegacyFindButton(const char *name);
bool LegacyHasButton(const char *name);
bool LegacyClick(const char *name);
bool LegacyClickFirst(std::initializer_list<const char *> names);
std::string LegacyCaption(const char *name);
bool LegacySetCaption(const char *name, const std::string &caption);
// Eclipse has no general enabled property or readable callback. Controllers
// must use their screen's real enable/disable semantics rather than guess one.

// Standard button captions use the existing txtCaption child convention.
// SetText also accepts UITextPoint/UITextParagraph and editable HD fields.
bool SetText(UIObject *object, const std::string &text);
std::string GetText(UIObject *object);
void SetVisible(UIObject *object, bool visible);
void SetEnabled(UIObject *object, bool enabled);
// These copy metadata only: existing hover/click callbacks remain owned by the
// control's controller. Legacy bindings re-resolve by name, never Button address.
void SetTooltip(UIObject *object, const std::string &text,
                const std::string &hotkeyHint = std::string());
void BindLegacyTooltip(UIObject *object, const std::string &buttonName,
                       const std::string &hotkeyHint = std::string());
// Requires an existing Eclipse editable button; no synthetic field is created.
bool SyncLegacyInput(const UIButtonTextField *field, const char *editableName);
bool SyncLegacyInputAndClick(const UIButtonTextField *field,
                             const char *editableName, const char *actionName);

// TakeLayout transfers each complete parse out of the manager, so the next row
// is independently parsed with its current layout and asset directories. With
// childPath set, only that template subtree is returned; the wrapper is freed.
class HDRepeatLayoutFactory {
public:
  explicit HDRepeatLayoutFactory(const std::string &layoutName,
                                 const std::string &childPath = std::string());
  std::unique_ptr<UIObject> Create();
  const std::string &GetError() const { return error; }

private:
  std::string layoutName, childPath, error;
};

// Own beside the input root and Update after the tree's frame update. The root
// must outlive this controller. No input routing is required: the authored host
// is entirely hit-test transparent. Only copied text and a hover revision are
// retained across frames, never a borrowed hovered control.
class HDTooltipController {
public:
  explicit HDTooltipController(UIObject &ownerRoot, float delay = 0.65f);
  HDTooltipController(const HDTooltipController &) = delete;
  HDTooltipController &operator=(const HDTooltipController &) = delete;
  void Update(float dt);
  void Hide();
  bool IsVisible() const { return view.IsVisible(); }

private:
  bool Prepare();
  UIObject &ownerRoot;
  HDLayoutView view;
  unsigned long long hoverRevision;
  float elapsed, delay, preparedWidth;
  std::string text, hotkeyHint, captionText;
};

class HDPopupController {
public:
  using Callback = std::function<void()>;
  explicit HDPopupController(UIObject &ownerRoot);
  ~HDPopupController() noexcept;
  HDPopupController(const HDPopupController &) = delete;
  HDPopupController &operator=(const HDPopupController &) = delete;

  bool ShowInfo(const std::string &title, const std::string &body,
                Callback onClose = Callback());
  bool ShowConfirm(const std::string &title, const std::string &body,
                   Callback onConfirm, Callback onCancel = Callback());
  // Updates an existing popup without reattaching it or resetting its focus.
  // The body remains complete and is scrollable when it exceeds the viewport.
  bool SetMessage(const std::string &title, const std::string &body);
  // Explicit Hide dismisses silently. Accept/Cancel hide before invoking user
  // code. Escape acknowledges an info popup or cancels a confirmation.
  void Hide();
  bool IsVisible() const;
  void Focus();
  void Accept();
  void Cancel();
  bool HandleKey(int key, unsigned modifiers = 0, bool down = true);
  // Route BEFORE hotkeys, normal HD dispatch, and Eclipse. While visible this
  // consumes every pointer/keyboard event, including the event that closes it.
  // Tab is confined to popup buttons; background HD fields never receive it.
  bool HandleInput(const ALLEGRO_EVENT &event);

private:
  bool Prepare();
  bool Show(const std::string &title, const std::string &body, bool confirmation,
            Callback onConfirm, Callback onCancel);
  void Finish(bool accepted);
  HDLayoutView view;
  Callback acceptCallback, cancelCallback;
  bool confirmation, finishing;
};

// Presents the live legacy message box, including animated captions. Button
// addresses are identity tokens only; every action re-resolves its real button.
class HDLegacyMessageBoxMirror {
public:
  explicit HDLegacyMessageBoxMirror(UIObject &ownerRoot);
  HDLegacyMessageBoxMirror(const HDLegacyMessageBoxMirror &) = delete;
  HDLegacyMessageBoxMirror &operator=(const HDLegacyMessageBoxMirror &) = delete;
  void Update();
  bool HandleInput(const ALLEGRO_EVENT &event);
  bool IsVisible() const;
  void Hide();

private:
  void Invoke(bool accepted);
  HDPopupController popup;
  std::uintptr_t backgroundIdentity, titleIdentity, textIdentity;
  std::uintptr_t acceptIdentity, cancelIdentity;
  const char *acceptName, *cancelName;
  std::string title, body;
};

// Adds a titlebar hit target to the movable window. It stores no borrowed
// window pointer: pointer events act on its current parent, raise that window's
// ancestry, and clamp the complete window to the 1920x1080 input root.
std::unique_ptr<UIObject> MakeDraggableTitle(float x, float y, float width,
                                           float height);

} // namespace HDUI
#endif
