#ifndef _included_hd_ui_object_h
#define _included_hd_ui_object_h

#include "hd_allegro5.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace HDUI {
class UIObject;
typedef std::function<void(UIObject*)> ClickCallback;
typedef std::function<void(UIObject*)> HoverCallback;

enum class ScalingType { ScaleNormal, ScaleByHeight, ScaleByWidth, ScaleToWidth, ScaleToHeight, ScaleFree };
enum class ArrangeType { ArrangeFree, ArrangeHorizontal, ArrangeVertical, ArrangeTableHorizontal, ArrangeTableVertical };
enum class TextAlign { Left=0, Center=1, Right=2 };
struct Geometry { float x,y,w,h,sx,sy,alpha; };

class UIObject {
public:
    UIObject();
    virtual ~UIObject();
    UIObject(const UIObject&) = delete;
    UIObject& operator=(const UIObject&) = delete;
    std::string name;
    float x,y,w,h,alpha,scaleX,scaleY;
    bool visible,enabled;
    int zOrder;
    bool interactive,focusable,clipChildren;
    // Children are exclusively owned; the parent and input links are borrowed.
    UIObject* parent;
    std::vector<std::unique_ptr<UIObject>> children;
    ScalingType scalingType;
    ArrangeType arrangeType;
    float arrangePadding;
    bool isHovered,isPressed;
    ClickCallback onClickCallback;
    HoverCallback onHoverCallback;
    // Tooltip metadata does not install callbacks or make a control interactive.
    std::string tooltipText,tooltipHotkeyHint,tooltipLegacyButton;
    virtual void Update(float dt);
    virtual void Draw();
    virtual void OnMouseEnter();
    virtual void OnMouseLeave();
    virtual void OnMouseDown();
    virtual void OnMouseUp();
    virtual void OnClick();
    virtual void OnPointerMove(float px,float py);
    virtual void OnPointerDown(float px,float py,int button);
    virtual void OnPointerUp(float px,float py,int button);
    virtual bool OnMouseWheel(float dx,float dy);
    virtual bool OnTextInput(int codepoint);
    virtual bool OnKey(int key,unsigned modifiers,bool down);
    virtual void OnFocusChanged(bool focused);
    virtual const char* GetTypeName() const { return "UIObject"; }
    void AddChild(std::unique_ptr<UIObject> child);
    std::unique_ptr<UIObject> DetachChild(UIObject* child);
    UIObject* FindChild(const std::string& name);
    UIObject* FindChildRecursive(const std::string& path);
    void RemoveChild(const std::string& name);
    void ClearChildren();
    void BringToFront();
    void SetZOrder(int order);
    void ArrangeChildren();
    Geometry GetGeometry() const;
    float GetAbsoluteX() const;
    float GetAbsoluteY() const;
    float GetAbsoluteWidth() const;
    float GetAbsoluteHeight() const;
    float GetAbsoluteAlpha() const;
    bool IsVisible() const;
    bool IsEnabled() const;
    bool ContainsPoint(float px,float py) const;
    virtual UIObject* HitTest(float px,float py);
    UIObject* GetRoot();
    bool IsDescendantOf(const UIObject* ancestor) const;
    // Pointer coordinates are display pixels, as supplied by Gucci/Allegro.
    bool DispatchEvent(const ALLEGRO_EVENT& event);
    bool DispatchMouseMove(float px,float py);
    bool DispatchMouseDown(float px,float py,int button=1);
    bool DispatchMouseUp(float px,float py,int button=1);
    bool DispatchMouseWheel(float px,float py,float dx,float dy);
    bool DispatchTextInput(int codepoint);
    bool DispatchKey(int key,unsigned modifiers=0,bool down=true);
    void SetFocus(UIObject* object);
    UIObject* GetFocus();
    UIObject* GetPointerCapture();
    // Borrow only until the next callback/tree mutation. Revisions never retain
    // a control address and change on hover leave, including tree destruction.
    UIObject* GetHovered();
    unsigned long long GetHoverRevision();
    void ReleasePointer();
    void ResetInput();
    void FocusNext(bool reverse=false);
protected:
    virtual float ChildOffsetX() const { return 0; }
    virtual float ChildOffsetY() const { return 0; }
    void ForgetInputFor(UIObject* subtree);
private:
    UIObject* hovered;
    UIObject* captured;
    UIObject* focused;
    int captureButton;
    unsigned long long hoverRevision;
    float pointerX,pointerY;
    bool pointerKnown;
    void UpdateHover(float px,float py);
    void ValidateInput();
    void VisitFocus(UIObject* current,bool& passed,UIObject*& first,UIObject*& last,
                    UIObject*& next,UIObject*& previous);
};

class UIContainer : public UIObject {
public:
    const char* GetTypeName() const override { return "Container"; }
};
class UIGradStop : public UIObject {
public:
    UIGradStop() : location(0),fillAlpha(1) {}
    float location,fillAlpha;
    std::string fillColor;
    const char* GetTypeName() const override { return "GradStop"; }
};
class UIGradient : public UIObject {
public:
    UIGradient();
    struct GradStop { float location; std::string fillColor; float fillAlpha=1; };
    std::vector<GradStop> stops;
    std::string subtype;
    void Draw() override;
    const char* GetTypeName() const override { return "Gradient"; }
};
class UIRectangle : public UIObject {
public:
    UIRectangle();
    std::string subtype,fillColor,strokeColor;
    float fillAlpha,strokeWidth;
    void Draw() override;
    const char* GetTypeName() const override { return "Rectangle"; }
};
class UILine : public UIObject {
public:
    UILine();
    float x1,y1,x2,y2,strokeWidth;
    std::string strokeColor;
    void Draw() override;
    const char* GetTypeName() const override { return "Line"; }
};
class UITextPoint : public UIObject {
public:
    UITextPoint();
    std::string text,font,assetBasePath;
    int fontSize;
    float leading;
    std::string fillColor;
    TextAlign align;
    void Update(float dt) override;
    void Draw() override;
    ALLEGRO_FONT* GetFont();
    float Measure(size_t begin,size_t end);
    virtual void PrepareText();
    virtual size_t CaretAt(float px,float py);
    virtual void CaretPosition(size_t index,float& px,float& py);
    const char* GetTypeName() const override { return "TextPoint"; }
protected:
    void DrawRange(size_t begin,size_t end,float px,float py);
    ALLEGRO_FONT* cachedFont;
private:
    std::string cachedName,cachedBase;
    int cachedSize;
    unsigned cachedGeneration;
};
class UITextParagraph : public UITextPoint {
public:
    struct TextLine { size_t begin,end; float width; };
    UITextParagraph();
    std::vector<TextLine> lines;
    bool wordWrap;
    void PrepareText() override;
    void Draw() override;
    size_t CaretAt(float px,float py) override;
    void CaretPosition(size_t index,float& px,float& py) override;
    const char* GetTypeName() const override { return "TextParagraph"; }
private:
    std::string wrappedText;
    float wrappedWidth;
    ALLEGRO_FONT* wrappedFont;
    bool wrappedMode;
};
class UIImage : public UIObject {
public:
    UIImage();
    ~UIImage() override;
    std::string file;
    void* bitmap;
    bool ownsbitmap;
    void Draw() override;
    void LoadBitmap(const std::string& basePath);
    const char* GetTypeName() const override { return "Image"; }
private:
    std::shared_ptr<ALLEGRO_BITMAP> texture;
};
class UIVertex : public UIObject {
public:
    const char* GetTypeName() const override { return "Vertex"; }
};
class UIPoly : public UIRectangle {
public:
    struct Vertex { float x,y; };
    std::vector<Vertex> vertices;
    void PrepareVertices();
    void Draw() override;
    const char* GetTypeName() const override { return "Poly"; }
private:
    std::vector<float> transformed;
    std::vector<Vertex> preparedVertices;
    std::vector<int> triangleIndices;
    std::vector<ALLEGRO_VERTEX> triangleVertices;
};
class UICircle : public UIRectangle {
public:
    void Draw() override;
    const char* GetTypeName() const override { return "Circle"; }
};
class UIArc : public UILine {
public:
    UIArc();
    float startAngle,endAngle;
    void Draw() override;
    const char* GetTypeName() const override { return "Arc"; }
};
class UIClippingMask : public UIContainer {
public:
    UIClippingMask() { clipChildren=true; }
    const char* GetTypeName() const override { return "ClippingMask"; }
};
class UIInteractive : public UIContainer {
public:
    UIInteractive() { interactive=true; }
    const char* GetTypeName() const override { return "Interactive"; }
};
class UIDraggable : public UIInteractive {
public:
    UIDraggable();
    bool dragHorizontal,dragVertical,bounded;
    float minX,minY,maxX,maxY;
    void OnPointerDown(float px,float py,int button) override;
    void OnPointerMove(float px,float py) override;
    const char* GetTypeName() const override { return "Draggable"; }
private:
    float dragX,dragY,startX,startY;
};
class UIButtonStandard : public UIInteractive {
public:
    UIButtonStandard();
    UIObject* gfxStandard;
    UIObject* gfxHover;
    UIObject* gfxClick;
    void Draw() override;
    void OnMouseEnter() override;
    void OnMouseLeave() override;
    void OnMouseDown() override;
    void OnMouseUp() override;
    bool OnKey(int key,unsigned modifiers,bool down) override;
    const char* GetTypeName() const override { return "ButtonStandard"; }
protected:
    virtual void UpdateStateGraphics();
    bool focusedState;
    void OnFocusChanged(bool focused) override;
};
class UIButtonSelect : public UIButtonStandard {
public:
    UIButtonSelect() : selected(true) {}
    bool selected;
    void OnClick() override;
    const char* GetTypeName() const override { return "ButtonSelect"; }
protected:
    void UpdateStateGraphics() override;
};
class UIButtonTextField : public UIButtonStandard {
public:
    UIButtonTextField();
    std::string inputText;
    bool isPassword;
    int maxLength;
    bool hasFocus;
    size_t caret;
    bool multiline;
    ClickCallback onEnterCallback;
    void SetText(const std::string& value);
    void Update(float dt) override;
    void OnClick() override;
    void OnPointerDown(float px,float py,int button) override;
    bool OnTextInput(int codepoint) override;
    bool OnKey(int key,unsigned modifiers,bool down) override;
    void OnFocusChanged(bool focused) override;
    void Draw() override;
    const char* GetTypeName() const override { return "ButtonTextField"; }
protected:
    UITextPoint* Caption();
    void SyncCaption();
private:
    std::string displayedText,promptText;
    bool initializedText;
    bool displayedPassword,displayedFocus;
    float blinkTime,viewOffsetX,viewOffsetY;
    size_t PreviousWord(size_t from) const;
    size_t NextWord(size_t from) const;
    void KeepCaretVisible();
};
class UIButtonTextFieldBox : public UIButtonTextField {
public:
    UIButtonTextFieldBox() { multiline=true; }
    const char* GetTypeName() const override { return "ButtonTextFieldBox"; }
};
class UIScrollableList : public UIInteractive {
public:
    UIScrollableList();
    ~UIScrollableList() override;
    float scrollX,scrollY,wheelStep;
    bool scrollHorizontal,scrollVertical,dragScroll;
    void Update(float dt) override;
    void Draw() override;
    UIObject* HitTest(float px,float py) override;
    void OnPointerDown(float px,float py,int button) override;
    void OnPointerMove(float px,float py) override;
    bool OnMouseWheel(float dx,float dy) override;
    void SetScroll(float sx,float sy);
    void ClampScroll();
    const char* GetTypeName() const override { return "ScrollableList"; }
protected:
    float ChildOffsetX() const override { return -scrollX; }
    float ChildOffsetY() const override { return -scrollY; }
private:
    float dragX,dragY,startX,startY;
    class Scrollbar;
    std::unique_ptr<Scrollbar> horizontalScrollbar,verticalScrollbar;
    bool triedHorizontalScrollbar,triedVerticalScrollbar;
    float viewportWidth,viewportHeight,maxScrollX,maxScrollY;
};
} // namespace HDUI
#endif
