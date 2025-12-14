/*
 * HD_UI Object Implementation
 */

#include "hd_ui_object.h"
#include "hd_allegro5.h"
#include "hd_atlas.h"
#include <algorithm>


namespace HDUI {

// ============================================================================
// UIObject
// ============================================================================

UIObject::UIObject()
    : x(0), y(0), w(0), h(0)
    , alpha(1.0f)
    , visible(true)
    , enabled(true)
    , parent(nullptr)
    , scalingType(ScalingType::ScaleNormal)
    , arrangeType(ArrangeType::ArrangeFree)
    , arrangePadding(0)
    , isHovered(false)
    , isPressed(false)
{
}

UIObject::~UIObject() {
    ClearChildren();
}

void UIObject::Update(float dt) {
    for (auto& child : children) {
        if (child) child->Update(dt);
    }
}

void UIObject::Draw() {
    if (!visible) return;
    
    for (auto& child : children) {
        if (child && child->visible) {
            child->Draw();
        }
    }
}

void UIObject::OnMouseEnter() {
    isHovered = true;
    if (onHoverCallback) onHoverCallback(this);
}

void UIObject::OnMouseLeave() {
    isHovered = false;
    isPressed = false;
}

void UIObject::OnMouseDown() {
    isPressed = true;
}

void UIObject::OnMouseUp() {
    if (isPressed && isHovered) {
        OnClick();
    }
    isPressed = false;
}

void UIObject::OnClick() {
    if (onClickCallback) onClickCallback(this);
}

void UIObject::AddChild(std::unique_ptr<UIObject> child) {
    if (child) {
        child->parent = this;
        children.push_back(std::move(child));
    }
}

UIObject* UIObject::FindChild(const std::string& childName) {
    for (auto& child : children) {
        if (child && child->name == childName) {
            return child.get();
        }
    }
    return nullptr;
}

UIObject* UIObject::FindChildRecursive(const std::string& path) {
    // Handle paths like "container/button/gfxHover"
    size_t pos = path.find('/');
    if (pos == std::string::npos) {
        return FindChild(path);
    }
    
    std::string first = path.substr(0, pos);
    std::string rest = path.substr(pos + 1);
    
    UIObject* child = FindChild(first);
    if (child) {
        return child->FindChildRecursive(rest);
    }
    return nullptr;
}

void UIObject::RemoveChild(const std::string& childName) {
    children.erase(
        std::remove_if(children.begin(), children.end(),
            [&childName](const std::unique_ptr<UIObject>& c) {
                return c && c->name == childName;
            }),
        children.end()
    );
}

void UIObject::ClearChildren() {
    children.clear();
}

float UIObject::GetAbsoluteX() const {
    float absX = x;
    if (parent) absX += parent->GetAbsoluteX();
    return absX;
}

float UIObject::GetAbsoluteY() const {
    float absY = y;
    if (parent) absY += parent->GetAbsoluteY();
    return absY;
}

bool UIObject::ContainsPoint(float px, float py) const {
    float ax = GetAbsoluteX();
    float ay = GetAbsoluteY();
    return px >= ax && px < ax + w && py >= ay && py < ay + h;
}

// ============================================================================
// UIContainer
// ============================================================================

UIContainer::UIContainer() : UIObject() {}
UIContainer::~UIContainer() {}

void UIContainer::Draw() {
    if (!visible) return;
    UIObject::Draw(); // Draw children
}

// ============================================================================
// UIGradient
// ============================================================================

UIGradient::UIGradient() : UIObject(), subtype("gradVertical") {}
UIGradient::~UIGradient() {}

void UIGradient::Draw() {
    if (!visible || stops.size() < 2) return;
    
    float ax = GetAbsoluteX();
    float ay = GetAbsoluteY();
    
    // Get first and last stop colors
    Color c1 = Color::FromHex(stops.front().fillColor.c_str());
    Color c2 = Color::FromHex(stops.back().fillColor.c_str());
    c1.a *= alpha;
    c2.a *= alpha;
    
    Allegro5System::DrawGradientVertical(ax, ay, w, h,
        c1.r, c1.g, c1.b, c1.a,
        c2.r, c2.g, c2.b, c2.a);
    
    UIObject::Draw();
}

// ============================================================================
// UIRectangle
// ============================================================================

UIRectangle::UIRectangle() 
    : UIObject()
    , subtype("filled")
    , fillAlpha(1.0f)
    , strokeWidth(1.0f)
{}

UIRectangle::~UIRectangle() {}

void UIRectangle::Draw() {
    if (!visible) return;
    
    float ax = GetAbsoluteX();
    float ay = GetAbsoluteY();
    
    Color fill = Color::FromHex(fillColor.c_str());
    fill.a = fillAlpha * alpha;
    
    Color stroke = Color::FromHex(strokeColor.c_str());
    stroke.a *= alpha;
    
    if (subtype == "filled" || subtype == "strokedFill") {
        Allegro5System::DrawRectFilled(ax, ay, w, h,
            fill.r, fill.g, fill.b, fill.a);
    }
    
    if (subtype == "stroked" || subtype == "strokedFill") {
        if (strokeWidth > 0) {
            Allegro5System::DrawRectOutline(ax, ay, w, h,
                stroke.r, stroke.g, stroke.b, stroke.a, strokeWidth);
        }
    }
    
    UIObject::Draw();
}

// ============================================================================
// UILine
// ============================================================================

UILine::UILine()
    : UIObject()
    , x1(0), y1(0), x2(0), y2(0)
    , strokeWidth(1.0f)
{}

UILine::~UILine() {}

void UILine::Draw() {
    if (!visible) return;
    
    float ax = GetAbsoluteX();
    float ay = GetAbsoluteY();
    
    Color c = Color::FromHex(strokeColor.c_str());
    c.a *= alpha;
    
    Allegro5System::DrawLine(ax + x1, ay + y1, ax + x2, ay + y2,
        c.r, c.g, c.b, c.a, strokeWidth);
    
    UIObject::Draw();
}

// ============================================================================
// UITextPoint
// ============================================================================

UITextPoint::UITextPoint()
    : UIObject()
    , fontSize(18)
    , align(TextAlign::Left)
{}

UITextPoint::~UITextPoint() {}

void UITextPoint::Draw() {
    if (!visible || text.empty()) return;
    
    float ax = GetAbsoluteX();
    float ay = GetAbsoluteY();
    
    Color c = Color::FromHex(fillColor.c_str());
    c.a *= alpha;
    
    // TODO: Use actual font from font manager
    // For now just log that we would draw text
    // Allegro5System::DrawText(font, ax, ay, text.c_str(), c.r, c.g, c.b, c.a, (int)align);
    
    UIObject::Draw();
}

// ============================================================================
// UIImage
// ============================================================================

UIImage::UIImage()
    : UIObject()
    , bitmap(nullptr)
    , ownsbitmap(false)
{}

UIImage::~UIImage() {
    // Only destroy if we own the bitmap (not from atlas)
    if (bitmap && ownsbitmap) {
        al_destroy_bitmap((ALLEGRO_BITMAP*)bitmap);
    }
    bitmap = nullptr;
}

void UIImage::Draw() {
    if (!visible) return;
    
    if (bitmap) {
        float ax = GetAbsoluteX();
        float ay = GetAbsoluteY();
        Allegro5System::DrawBitmap((ALLEGRO_BITMAP*)bitmap, ax, ay, alpha);
    }
    
    UIObject::Draw();
}

void UIImage::LoadBitmap(const std::string& basePath) {
    // First try to get from atlas
    ALLEGRO_BITMAP* atlasTex = AtlasManager::GetInstance().GetTexture(file);
    if (atlasTex) {
        bitmap = (void*)atlasTex;
        ownsbitmap = false; // Atlas owns this bitmap
        return;
    }
    
    // Fallback: load individual file
    std::string fullPath = basePath + "/" + file;
    bitmap = (void*)Allegro5System::LoadBitmap(fullPath.c_str());
    ownsbitmap = (bitmap != nullptr); // We own if we loaded it
}


// ============================================================================
// UIButtonStandard
// ============================================================================

UIButtonStandard::UIButtonStandard()
    : UIObject()
    , gfxStandard(nullptr)
    , gfxHover(nullptr)
    , gfxClick(nullptr)
{}

UIButtonStandard::~UIButtonStandard() {}

void UIButtonStandard::Draw() {
    if (!visible) return;
    
    // Show appropriate state graphic
    UpdateStateGraphics();
    
    UIObject::Draw();
}

void UIButtonStandard::UpdateStateGraphics() {
    // Find state graphics by name
    if (!gfxStandard) gfxStandard = FindChild("gfxStandard");
    if (!gfxHover) gfxHover = FindChild("gfxHover");
    if (!gfxClick) gfxClick = FindChild("gfxClick");
    
    // Set visibility based on state
    if (gfxStandard) gfxStandard->visible = !isHovered && !isPressed;
    if (gfxHover) gfxHover->visible = isHovered && !isPressed;
    if (gfxClick) gfxClick->visible = isPressed;
}

void UIButtonStandard::OnMouseEnter() {
    UIObject::OnMouseEnter();
    UpdateStateGraphics();
}

void UIButtonStandard::OnMouseLeave() {
    UIObject::OnMouseLeave();
    UpdateStateGraphics();
}

void UIButtonStandard::OnMouseDown() {
    UIObject::OnMouseDown();
    UpdateStateGraphics();
}

void UIButtonStandard::OnMouseUp() {
    UIObject::OnMouseUp();
    UpdateStateGraphics();
}

// ============================================================================
// UIButtonTextField
// ============================================================================

UIButtonTextField::UIButtonTextField()
    : UIButtonStandard()
    , isPassword(false)
    , maxLength(256)
    , hasFocus(false)
{}

UIButtonTextField::~UIButtonTextField() {}

void UIButtonTextField::OnClick() {
    hasFocus = true;
    UIButtonStandard::OnClick();
}

void UIButtonTextField::OnTextInput(char c) {
    if (!hasFocus) return;
    
    if (c == '\b') {
        // Backspace
        if (!inputText.empty()) {
            inputText.pop_back();
        }
    } else if (c >= 32 && c < 127) {
        // Printable character
        if ((int)inputText.length() < maxLength) {
            inputText += c;
        }
    }
}

void UIButtonTextField::Draw() {
    if (!visible) return;
    
    UIButtonStandard::Draw();
    
    // Draw the text (or asterisks for password)
    UITextPoint* txtCaption = dynamic_cast<UITextPoint*>(FindChild("txtCaption"));
    if (txtCaption) {
        if (isPassword && !inputText.empty()) {
            txtCaption->text = std::string(inputText.length(), '*');
        } else if (!inputText.empty()) {
            txtCaption->text = inputText;
        }
    }
}

} // namespace HDUI
