/*
 * HD_UI Object Base Class
 * Base class for all HD UI elements
 */

#ifndef _included_hd_ui_object_h
#define _included_hd_ui_object_h

#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace HDUI {

// Forward declarations
class UIObject;
class Layout;

// Callback types
typedef std::function<void(UIObject*)> ClickCallback;
typedef std::function<void(UIObject*)> HoverCallback;

// Scaling types from XML
enum class ScalingType {
    ScaleNormal,
    ScaleByHeight,
    ScaleByWidth
};

// Arrangement types from XML
enum class ArrangeType {
    ArrangeFree,
    ArrangeHorizontal,
    ArrangeVertical
};

// Base UI Object class
class UIObject {
public:
    UIObject();
    virtual ~UIObject();
    
    // Core properties
    std::string name;
    float x, y, w, h;
    float alpha;
    bool visible;
    bool enabled;
    
    // Hierarchy
    UIObject* parent;
    std::vector<std::unique_ptr<UIObject>> children;
    
    // Layout properties
    ScalingType scalingType;
    ArrangeType arrangeType;
    float arrangePadding;
    
    // Virtual methods
    virtual void Update(float dt);
    virtual void Draw();
    virtual void OnMouseEnter();
    virtual void OnMouseLeave();
    virtual void OnMouseDown();
    virtual void OnMouseUp();
    virtual void OnClick();
    
    // Hierarchy management
    void AddChild(std::unique_ptr<UIObject> child);
    UIObject* FindChild(const std::string& name);
    UIObject* FindChildRecursive(const std::string& path);
    void RemoveChild(const std::string& name);
    void ClearChildren();
    
    // Coordinate helpers
    float GetAbsoluteX() const;
    float GetAbsoluteY() const;
    bool ContainsPoint(float px, float py) const;
    
    // State
    bool isHovered;
    bool isPressed;
    
    // Callbacks
    ClickCallback onClickCallback;
    HoverCallback onHoverCallback;
    
    // Type identification
    virtual const char* GetTypeName() const { return "UIObject"; }
};

// Text rendering alignment
enum class TextAlign {
    Left = 0,
    Center = 1,
    Right = 2
};

// Container - groups other UI elements
class UIContainer : public UIObject {
public:
    UIContainer();
    ~UIContainer() override;
    
    void Draw() override;
    const char* GetTypeName() const override { return "Container"; }
};

// Gradient background
class UIGradient : public UIObject {
public:
    UIGradient();
    ~UIGradient() override;
    
    struct GradStop {
        float location;
        std::string fillColor;
    };
    
    std::vector<GradStop> stops;
    std::string subtype; // "gradVertical", "gradHorizontal"
    
    void Draw() override;
    const char* GetTypeName() const override { return "Gradient"; }
};

// Rectangle
class UIRectangle : public UIObject {
public:
    UIRectangle();
    ~UIRectangle() override;
    
    std::string subtype; // "filled", "stroked", "strokedFill"
    std::string fillColor;
    float fillAlpha;
    std::string strokeColor;
    float strokeWidth;
    
    void Draw() override;
    const char* GetTypeName() const override { return "Rectangle"; }
};

// Line
class UILine : public UIObject {
public:
    UILine();
    ~UILine() override;
    
    float x1, y1, x2, y2;
    std::string strokeColor;
    float strokeWidth;
    
    void Draw() override;
    const char* GetTypeName() const override { return "Line"; }
};

// Text point (single line text)
class UITextPoint : public UIObject {
public:
    UITextPoint();
    ~UITextPoint() override;
    
    std::string text;
    std::string font;
    int fontSize;
    std::string fillColor;
    TextAlign align;
    
    void Draw() override;
    const char* GetTypeName() const override { return "TextPoint"; }
};

// Image
class UIImage : public UIObject {
public:
    UIImage();
    ~UIImage() override;
    
    std::string file;
    void* bitmap; // ALLEGRO_BITMAP*, stored as void* to avoid header dep
    
    void Draw() override;
    void LoadBitmap(const std::string& basePath);
    const char* GetTypeName() const override { return "Image"; }
};

// Standard button with hover/click states
class UIButtonStandard : public UIObject {
public:
    UIButtonStandard();
    ~UIButtonStandard() override;
    
    // Child graphics for different states (found by name)
    UIObject* gfxStandard;
    UIObject* gfxHover;
    UIObject* gfxClick;
    
    void Draw() override;
    void OnMouseEnter() override;
    void OnMouseLeave() override;
    void OnMouseDown() override;
    void OnMouseUp() override;
    const char* GetTypeName() const override { return "ButtonStandard"; }
    
private:
    void UpdateStateGraphics();
};

// Text field button (input field)
class UIButtonTextField : public UIButtonStandard {
public:
    UIButtonTextField();
    ~UIButtonTextField() override;
    
    std::string inputText;
    bool isPassword;
    int maxLength;
    bool hasFocus;
    
    void OnClick() override;
    void OnTextInput(char c);
    void Draw() override;
    const char* GetTypeName() const override { return "ButtonTextField"; }
};

} // namespace HDUI

#endif // _included_hd_ui_object_h
