#include "hd_ui_object.h"
#include "hd_atlas.h"
#include "hd_layout_parser.h"
#include <algorithm>
#include <cmath>

namespace HDUI {
namespace {
float Clamp(float value,float low,float high) { return std::max(low,std::min(value,high)); }
size_t NextCharacter(const std::string& text,size_t i) {
    if (i >= text.size()) return text.size();
    ++i;
    while (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80) ++i;
    return i;
}
size_t PreviousCharacter(const std::string& text,size_t i) {
    if (!i) return 0;
    --i;
    while (i && (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80) --i;
    return i;
}
size_t CharacterCount(const std::string& text) {
    size_t count=0;
    for (size_t i=0;i<text.size();i=NextCharacter(text,i)) ++count;
    return count;
}
bool WordSpace(const std::string& text,size_t i) {
    return i < text.size() && (text[i]==' ' || text[i]=='\t' || text[i]=='\r' || text[i]=='\n');
}
struct Clip {
    int x,y,w,h;
    explicit Clip(const Geometry& geometry) {
        al_get_clipping_rectangle(&x,&y,&w,&h);
        const int left=std::max(x,static_cast<int>(std::floor(geometry.x)));
        const int top=std::max(y,static_cast<int>(std::floor(geometry.y)));
        const int right=std::min(x+w,static_cast<int>(std::ceil(geometry.x+geometry.w)));
        const int bottom=std::min(y+h,static_cast<int>(std::ceil(geometry.y+geometry.h)));
        al_set_clipping_rectangle(left,top,std::max(0,right-left),std::max(0,bottom-top));
    }
    ~Clip() { al_set_clipping_rectangle(x,y,w,h); }
};
Color ObjectColor(const std::string& value,float alpha) {
    return Color::FromHexWithAlpha(value.c_str(),alpha);
}
bool Filled(const std::string& subtype) { return subtype != "stroked"; }
bool Stroked(const std::string& subtype) { return subtype != "filled"; }
float LineOffset(TextAlign align,float width,float available) {
    return align==TextAlign::Center ? (available-width)*0.5f : align==TextAlign::Right ? available-width : 0;
}
void ContentBounds(UIObject& object,float& right,float& bottom) {
    if (!object.IsVisible() || dynamic_cast<UIClippingMask*>(&object) ||
        dynamic_cast<UIGradStop*>(&object) || dynamic_cast<UIVertex*>(&object)) return;
    const Geometry geometry=object.GetGeometry();
    float width=geometry.w,height=geometry.h;
    if (UITextPoint* text=dynamic_cast<UITextPoint*>(&object)) {
        if (text->w<=0) {
            width=text->Measure(0,text->text.size())*geometry.sx;
            if (text->align==TextAlign::Center) width*=0.5f;
            else if (text->align==TextAlign::Right) width=0;
        }
        if (text->h<=0 && text->GetFont()) {
            const float lineHeight=static_cast<float>(al_get_font_line_height(text->GetFont()));
            height=lineHeight*geometry.sy;
            if (UITextParagraph* paragraph=dynamic_cast<UITextParagraph*>(text)) {
                paragraph->PrepareText();
                height=paragraph->lines.empty() ? 0 : (lineHeight+
                    static_cast<float>(paragraph->lines.size()-1)*std::max(1.0f,lineHeight+text->leading))*geometry.sy;
            }
        }
    }
    right=std::max(right,geometry.x+width);
    bottom=std::max(bottom,geometry.y+height);
    if (!object.clipChildren) for (auto& child : object.children) if (child) ContentBounds(*child,right,bottom);
}
}

UIObject::UIObject()
    : x(0),y(0),w(0),h(0),alpha(1),scaleX(1),scaleY(1),visible(true),enabled(true),
      zOrder(0),interactive(false),focusable(false),clipChildren(false),parent(nullptr),
      scalingType(ScalingType::ScaleNormal),arrangeType(ArrangeType::ArrangeFree),arrangePadding(0),
      isHovered(false),isPressed(false),hovered(nullptr),captured(nullptr),focused(nullptr),captureButton(0),hoverRevision(1),
      pointerX(0),pointerY(0),pointerKnown(false) {}
UIObject::~UIObject() {
    if (Allegro5System::GetInputRoot()==this) Allegro5System::SetInputRoot(nullptr);
    ForgetInputFor(this);
    // Detach before child destruction so children cannot walk a dying root.
    for (auto& child : children) if (child) child->parent=nullptr;
}
UIObject* UIObject::GetRoot() { UIObject* root=this; while (root->parent) root=root->parent; return root; }
bool UIObject::IsDescendantOf(const UIObject* ancestor) const {
    for (const UIObject* object=this;object;object=object->parent) if (object==ancestor) return true;
    return false;
}
void UIObject::ForgetInputFor(UIObject* subtree) {
    UIObject* root=GetRoot();
    if (root->hovered && root->hovered->IsDescendantOf(subtree)) {
        root->hovered=nullptr;
        ++root->hoverRevision;
    }
    if (root->captured && root->captured->IsDescendantOf(subtree)) { root->captured=nullptr; root->captureButton=0; }
    if (root->focused && root->focused->IsDescendantOf(subtree)) root->focused=nullptr;
}
void UIObject::Update(float dt) {
    ArrangeChildren();
    for (auto& child : children) if (child) child->Update(dt);
    if (!parent) ValidateInput();
}
void UIObject::Draw() {
    if (!IsVisible()) return;
    bool clipped=clipChildren;
    Geometry bounds=GetGeometry();
    // GOLD masks are authored as siblings before the content they constrain.
    // Scope their clipping to this parent, then restore the caller's clip.
    for (const auto& child : children) if (child && child->IsVisible() && dynamic_cast<UIClippingMask*>(child.get())) {
        const Geometry mask=child->GetGeometry();
        if (!clipped) { bounds=mask; clipped=true; }
        else {
            const float right=std::min(bounds.x+bounds.w,mask.x+mask.w);
            const float bottom=std::min(bounds.y+bounds.h,mask.y+mask.h);
            bounds.x=std::max(bounds.x,mask.x); bounds.y=std::max(bounds.y,mask.y);
            bounds.w=std::max(0.0f,right-bounds.x); bounds.h=std::max(0.0f,bottom-bounds.y);
        }
    }
    if (clipped) {
        Clip clip(bounds);
        for (auto& child : children) if (child && child->visible) child->Draw();
    } else {
        for (auto& child : children) if (child && child->visible) child->Draw();
    }
}
void UIObject::OnMouseEnter() {
    isHovered=true;
    ++GetRoot()->hoverRevision;
    if (onHoverCallback) onHoverCallback(this);
}
void UIObject::OnMouseLeave() { isHovered=false; ++GetRoot()->hoverRevision; }
void UIObject::OnMouseDown() { if (IsEnabled()) isPressed=true; }
void UIObject::OnMouseUp() {
    const bool click=isPressed && isHovered && IsEnabled();
    isPressed=false;
    if (click) OnClick();
}
void UIObject::OnClick() { if (IsEnabled() && onClickCallback) onClickCallback(this); }
void UIObject::OnPointerMove(float,float) {}
void UIObject::OnPointerDown(float,float,int button) { if (button==1) OnMouseDown(); }
void UIObject::OnPointerUp(float,float,int button) { if (button==1) OnMouseUp(); }
bool UIObject::OnMouseWheel(float,float) { return false; }
bool UIObject::OnTextInput(int) { return false; }
bool UIObject::OnKey(int,unsigned,bool) { return false; }
void UIObject::OnFocusChanged(bool) {}
void UIObject::AddChild(std::unique_ptr<UIObject> child) {
    if (!child) return;
    // Reject duplicate ownership and cycles without deleting the existing owner.
    if (child->parent || IsDescendantOf(child.get())) { child.release(); return; }
    child->ResetInput();
    child->parent=this;
    const auto position=std::upper_bound(children.begin(),children.end(),child->zOrder,
        [](int order,const std::unique_ptr<UIObject>& object) { return order<object->zOrder; });
    children.insert(position,std::move(child));
    ArrangeChildren();
}
std::unique_ptr<UIObject> UIObject::DetachChild(UIObject* object) {
    for (auto it=children.begin();it!=children.end();++it) {
        if (it->get()==object) {
            UIObject* root=GetRoot();
            if (root->focused && root->focused->IsDescendantOf(object)) root->SetFocus(nullptr);
            if (root->captured && root->captured->IsDescendantOf(object)) root->ReleasePointer();
            if (root->hovered && root->hovered->IsDescendantOf(object)) {
                UIObject* previous=root->hovered; root->hovered=nullptr; previous->OnMouseLeave();
            }
            std::unique_ptr<UIObject> result=std::move(*it);
            children.erase(it);
            result->parent=nullptr;
            ArrangeChildren();
            return result;
        }
    }
    return nullptr;
}
UIObject* UIObject::FindChild(const std::string& childName) {
    for (auto& child : children) if (child && child->name==childName) return child.get();
    return nullptr;
}
UIObject* UIObject::FindChildRecursive(const std::string& path) {
    if (path.empty() || path==name) return this;
    const size_t first=path[0]=='/' ? 1 : 0;
    const size_t slash=path.find('/',first);
    if (slash!=std::string::npos) {
        const std::string part=path.substr(first,slash-first);
        if (part==name || part.empty()) return FindChildRecursive(path.substr(slash+1));
        UIObject* child=FindChild(part);
        return child ? child->FindChildRecursive(path.substr(slash+1)) : nullptr;
    }
    const std::string childName=first ? path.substr(first) : path;
    if (UIObject* direct=FindChild(childName)) return direct;
    for (auto& child : children) if (child) {
        if (UIObject* found=child->FindChildRecursive(childName)) return found;
    }
    return nullptr;
}
void UIObject::RemoveChild(const std::string& childName) {
    for (size_t i=0;i<children.size();) {
        if (children[i] && children[i]->name==childName) DetachChild(children[i].get());
        else ++i;
    }
}
void UIObject::ClearChildren() {
    UIObject* root=GetRoot();
    if (root->focused && root->focused!=this && root->focused->IsDescendantOf(this)) root->SetFocus(nullptr);
    if (root->captured && root->captured!=this && root->captured->IsDescendantOf(this)) root->ReleasePointer();
    if (root->hovered && root->hovered!=this && root->hovered->IsDescendantOf(this)) {
        UIObject* previous=root->hovered; root->hovered=nullptr; previous->OnMouseLeave();
    }
    for (auto& child : children) if (child) child->parent=nullptr;
    children.clear();
}
void UIObject::BringToFront() {
    if (!parent || parent->children.empty()) return;
    auto& siblings=parent->children;
    for (auto it=siblings.begin();it!=siblings.end();++it) if (it->get()==this) {
        zOrder=std::max(zOrder,siblings.back()->zOrder);
        std::rotate(it,it+1,siblings.end());
        return;
    }
}
void UIObject::SetZOrder(int order) {
    zOrder=order;
    if (!parent) return;
    auto& siblings=parent->children;
    for (auto it=siblings.begin();it!=siblings.end();++it) if (it->get()==this) {
        std::unique_ptr<UIObject> self=std::move(*it);
        siblings.erase(it);
        auto position=std::upper_bound(siblings.begin(),siblings.end(),order,
            [](int z,const std::unique_ptr<UIObject>& object) { return z<object->zOrder; });
        siblings.insert(position,std::move(self));
        return;
    }
}
void UIObject::ArrangeChildren() {
    if (arrangeType==ArrangeType::ArrangeFree) return;
    const float padding=std::max(0.0f,arrangePadding);
    float cursorX=padding,cursorY=padding,rowHeight=0,columnWidth=0;
    for (auto& child : children) {
        if (!child || !child->visible || dynamic_cast<UIClippingMask*>(child.get()) ||
            dynamic_cast<UIGradStop*>(child.get()) || dynamic_cast<UIVertex*>(child.get())) continue;
        const float width=std::max(0.0f,child->w*child->scaleX);
        const float height=std::max(0.0f,child->h*child->scaleY);
        if (arrangeType==ArrangeType::ArrangeHorizontal) {
            child->x=cursorX; cursorX+=width+padding;
        } else if (arrangeType==ArrangeType::ArrangeVertical) {
            child->y=cursorY; cursorY+=height+padding;
        } else if (arrangeType==ArrangeType::ArrangeTableVertical) {
            // Vertical tables fill rows; horizontal tables fill columns.
            if (cursorX>padding && w>0 && cursorX+width>w) { cursorX=padding; cursorY+=rowHeight+padding; rowHeight=0; }
            child->x=cursorX; child->y=cursorY;
            cursorX+=width+padding; rowHeight=std::max(rowHeight,height);
        } else {
            if (cursorY>padding && h>0 && cursorY+height>h) { cursorY=padding; cursorX+=columnWidth+padding; columnWidth=0; }
            child->x=cursorX; child->y=cursorY;
            cursorY+=height+padding; columnWidth=std::max(columnWidth,width);
        }
    }
}
Geometry UIObject::GetGeometry() const {
    const Geometry inherited=parent ? parent->GetGeometry() : Geometry{0,0,0,0,Allegro5System::ScaleW(1),Allegro5System::ScaleH(1),1};
    float sx=inherited.sx,sy=inherited.sy;
    switch (scalingType) {
        case ScalingType::ScaleByWidth: sy=sx; break;
        case ScalingType::ScaleByHeight: sx=sy; break;
        case ScalingType::ScaleToWidth: sy=1; break;
        case ScalingType::ScaleToHeight: sx=1; break;
        case ScalingType::ScaleFree: break;
        default: break;
    }
    sx*=scaleX; sy*=scaleY;
    return Geometry{inherited.x+(x+(parent ? parent->ChildOffsetX() : 0))*inherited.sx,
                    inherited.y+(y+(parent ? parent->ChildOffsetY() : 0))*inherited.sy,
                    std::max(0.0f,w*sx),std::max(0.0f,h*sy),sx,sy,Clamp(inherited.alpha*alpha,0,1)};
}
float UIObject::GetAbsoluteX() const { return GetGeometry().x; }
float UIObject::GetAbsoluteY() const { return GetGeometry().y; }
float UIObject::GetAbsoluteWidth() const { return GetGeometry().w; }
float UIObject::GetAbsoluteHeight() const { return GetGeometry().h; }
float UIObject::GetAbsoluteAlpha() const { return GetGeometry().alpha; }
bool UIObject::IsVisible() const {
    for (const UIObject* object=this;object;object=object->parent) if (!object->visible || object->alpha<=0) return false;
    return true;
}
bool UIObject::IsEnabled() const {
    for (const UIObject* object=this;object;object=object->parent) if (!object->enabled) return false;
    return true;
}
bool UIObject::ContainsPoint(float px,float py) const {
    const Geometry geometry=GetGeometry();
    return px>=geometry.x && py>=geometry.y && px<geometry.x+geometry.w && py<geometry.y+geometry.h;
}
UIObject* UIObject::HitTest(float px,float py) {
    if (!IsVisible() || !IsEnabled() || (clipChildren && !ContainsPoint(px,py))) return nullptr;
    for (const auto& child : children) if (child && child->IsVisible() &&
        dynamic_cast<UIClippingMask*>(child.get()) && !child->ContainsPoint(px,py)) return nullptr;
    for (auto it=children.rbegin();it!=children.rend();++it) if (*it) {
        if (UIObject* hit=(*it)->HitTest(px,py)) return hit;
    }
    return (interactive || onClickCallback || onHoverCallback) && ContainsPoint(px,py) ? this : nullptr;
}
void UIObject::ValidateInput() {
    if (hovered && (!hovered->IsVisible() || !hovered->IsEnabled())) {
        UIObject* previous=hovered; hovered=nullptr; previous->OnMouseLeave();
    }
    if (captured && (!captured->IsVisible() || !captured->IsEnabled())) ReleasePointer();
    if (focused && (!focused->IsVisible() || !focused->IsEnabled())) SetFocus(nullptr);
}
void UIObject::UpdateHover(float px,float py) {
    UIObject* hit=HitTest(px,py);
    if (hovered!=hit) {
        UIObject* previous=hovered; hovered=hit;
        if (previous) previous->OnMouseLeave();
        if (hovered) hovered->OnMouseEnter();
    }
}
bool UIObject::DispatchMouseMove(float px,float py) {
    UIObject* root=GetRoot(); root->ValidateInput();
    root->pointerX=px; root->pointerY=py; root->pointerKnown=true;
    root->UpdateHover(px,py);
    UIObject* target=root->captured ? root->captured : root->hovered;
    if (target) target->OnPointerMove(px,py);
    return target!=nullptr;
}
bool UIObject::DispatchMouseDown(float px,float py,int button) {
    UIObject* root=GetRoot(); root->DispatchMouseMove(px,py);
    if (root->captured) return true;
    UIObject* hit=root->hovered;
    if (!hit) { root->SetFocus(nullptr); return false; }
    UIObject* focus=hit;
    while (focus && !focus->focusable) focus=focus->parent;
    root->SetFocus(focus);
    // Focus callbacks may have removed the hit object.
    hit=root->hovered;
    if (!hit) return true;
    root->captured=hit; root->captureButton=button;
    hit->OnPointerDown(px,py,button);
    return true;
}
bool UIObject::DispatchMouseUp(float px,float py,int button) {
    UIObject* root=GetRoot(); root->DispatchMouseMove(px,py);
    UIObject* target=root->captured;
    if (!target || root->captureButton!=button) return false;
    root->captured=nullptr; root->captureButton=0;
    target->isHovered=target->ContainsPoint(px,py) && root->hovered==target;
    target->OnPointerUp(px,py,button);
    return true;
}
bool UIObject::DispatchMouseWheel(float px,float py,float dx,float dy) {
    UIObject* root=GetRoot(); root->ValidateInput();
    for (UIObject* hit=root->HitTest(px,py);hit;hit=hit->parent) if (hit->OnMouseWheel(dx,dy)) return true;
    return false;
}
bool UIObject::DispatchTextInput(int codepoint) {
    UIObject* root=GetRoot(); root->ValidateInput();
    return root->focused && root->focused->OnTextInput(codepoint);
}
bool UIObject::DispatchKey(int key,unsigned modifiers,bool down) {
    UIObject* root=GetRoot(); root->ValidateInput();
    if (down && key==ALLEGRO_KEY_TAB) { root->FocusNext((modifiers & ALLEGRO_KEYMOD_SHIFT)!=0); return true; }
    return root->focused && root->focused->OnKey(key,modifiers,down);
}
bool UIObject::DispatchEvent(const ALLEGRO_EVENT& event) {
    switch (event.type) {
        case ALLEGRO_EVENT_MOUSE_AXES: {
            const bool moved=DispatchMouseMove(event.mouse.x,event.mouse.y);
            if (event.mouse.dz || event.mouse.dw) return DispatchMouseWheel(event.mouse.x,event.mouse.y,event.mouse.dw,event.mouse.dz) || moved;
            return moved;
        }
        case ALLEGRO_EVENT_MOUSE_BUTTON_DOWN: return DispatchMouseDown(event.mouse.x,event.mouse.y,event.mouse.button);
        case ALLEGRO_EVENT_MOUSE_BUTTON_UP: return DispatchMouseUp(event.mouse.x,event.mouse.y,event.mouse.button);
        case ALLEGRO_EVENT_KEY_CHAR:
            if (event.keyboard.unichar>=32 && !(event.keyboard.modifiers & (ALLEGRO_KEYMOD_CTRL|ALLEGRO_KEYMOD_ALT)) &&
                DispatchTextInput(event.keyboard.unichar)) return true;
            return DispatchKey(event.keyboard.keycode,event.keyboard.modifiers,true);
        case ALLEGRO_EVENT_KEY_UP: return DispatchKey(event.keyboard.keycode,event.keyboard.modifiers,false);
        case ALLEGRO_EVENT_DISPLAY_RESIZE:
            if (event.display.source==Allegro5System::GetDisplay())
                Allegro5System::SetDisplaySize(event.display.width,event.display.height);
            return false;
        case ALLEGRO_EVENT_DISPLAY_SWITCH_OUT: ResetInput(); return false;
        case ALLEGRO_EVENT_MOUSE_LEAVE_DISPLAY:
            GetRoot()->pointerKnown=false;
            if (GetRoot()->hovered) { UIObject* previous=GetRoot()->hovered; GetRoot()->hovered=nullptr; previous->OnMouseLeave(); }
            return false;
        default: return false;
    }
}
void UIObject::SetFocus(UIObject* object) {
    UIObject* root=GetRoot();
    if (object && (!object->IsDescendantOf(root) || !object->focusable || !object->IsEnabled() || !object->IsVisible())) object=nullptr;
    if (root->focused==object) return;
    UIObject* previous=root->focused; root->focused=object;
    if (previous) previous->OnFocusChanged(false);
    if (root->focused) root->focused->OnFocusChanged(true);
}
UIObject* UIObject::GetFocus() { return GetRoot()->focused; }
UIObject* UIObject::GetPointerCapture() { return GetRoot()->captured; }
UIObject* UIObject::GetHovered() {
    UIObject* root=GetRoot();
    root->ValidateInput();
    // A scroll, window move or replacement can change hover without an OS
    // pointer event. Re-hit-test without delivering a synthetic drag move.
    if (root->pointerKnown) root->UpdateHover(root->pointerX,root->pointerY);
    return root->hovered;
}
unsigned long long UIObject::GetHoverRevision() { return GetRoot()->hoverRevision; }
void UIObject::ReleasePointer() {
    UIObject* root=GetRoot();
    if (root->captured) root->captured->isPressed=false;
    root->captured=nullptr; root->captureButton=0;
}
void UIObject::ResetInput() {
    UIObject* root=GetRoot(); root->ReleasePointer(); root->SetFocus(nullptr);
    root->pointerKnown=false;
    if (root->hovered) { UIObject* previous=root->hovered; root->hovered=nullptr; previous->OnMouseLeave(); }
}
void UIObject::VisitFocus(UIObject* current,bool& passed,UIObject*& first,UIObject*& last,UIObject*& next,UIObject*& previous) {
    if (!IsVisible() || !IsEnabled()) return;
    if (focusable) {
        if (!first) first=this;
        if (this==current) { previous=last; passed=true; }
        else if (passed && !next) next=this;
        last=this;
    }
    for (auto& child : children) if (child) child->VisitFocus(current,passed,first,last,next,previous);
}
void UIObject::FocusNext(bool reverse) {
    UIObject* root=GetRoot();
    UIObject *first=nullptr,*last=nullptr,*next=nullptr,*previous=nullptr;
    bool passed=root->focused==nullptr;
    root->VisitFocus(root->focused,passed,first,last,next,previous);
    root->SetFocus(reverse ? (previous ? previous : last) : (next ? next : first));
}

UIGradient::UIGradient() : subtype("gradVertical") {}
void UIGradient::Draw() {
    if (!IsVisible()) return;
    if (stops.size()>=2) {
        const Geometry geometry=GetGeometry();
        const bool vertical=subtype!="gradHorizontal";
        for (size_t i=1;i<stops.size();++i) {
            const GradStop& from=stops[i-1]; const GradStop& to=stops[i];
            if (to.location<=from.location) continue;
            Color a=ObjectColor(from.fillColor,geometry.alpha*from.fillAlpha);
            Color b=ObjectColor(to.fillColor,geometry.alpha*to.fillAlpha);
            const float low=Clamp(from.location,0,1),high=Clamp(to.location,0,1);
            ALLEGRO_VERTEX quad[4]={};
            const float left=geometry.x+(vertical ? 0 : geometry.w*low);
            const float right=geometry.x+(vertical ? geometry.w : geometry.w*high);
            const float top=geometry.y+(vertical ? geometry.h*low : 0);
            const float bottom=geometry.y+(vertical ? geometry.h*high : geometry.h);
            quad[0].x=left; quad[0].y=top; quad[0].color=a.ToAllegro();
            quad[1].x=right; quad[1].y=top; quad[1].color=vertical ? a.ToAllegro() : b.ToAllegro();
            quad[2].x=right; quad[2].y=bottom; quad[2].color=b.ToAllegro();
            quad[3].x=left; quad[3].y=bottom; quad[3].color=vertical ? b.ToAllegro() : a.ToAllegro();
            al_draw_prim(quad,nullptr,nullptr,0,4,ALLEGRO_PRIM_TRIANGLE_FAN);
        }
    }
    UIObject::Draw();
}
UIRectangle::UIRectangle() : subtype("filled"),fillColor("FFFFFF"),strokeColor("FFFFFF"),fillAlpha(1),strokeWidth(1) {}
void UIRectangle::Draw() {
    if (!IsVisible()) return;
    const Geometry g=GetGeometry();
    if (Filled(subtype)) al_draw_filled_rectangle(g.x,g.y,g.x+g.w,g.y+g.h,ObjectColor(fillColor,g.alpha*fillAlpha).ToAllegro());
    if (Stroked(subtype) && strokeWidth>0) al_draw_rectangle(g.x,g.y,g.x+g.w,g.y+g.h,
        ObjectColor(strokeColor,g.alpha).ToAllegro(),strokeWidth*std::min(g.sx,g.sy));
    UIObject::Draw();
}
UILine::UILine() : x1(0),y1(0),x2(0),y2(0),strokeWidth(1),strokeColor("FFFFFF") {}
void UILine::Draw() {
    if (!IsVisible()) return;
    const Geometry g=GetGeometry();
    al_draw_line(g.x+x1*g.sx,g.y+y1*g.sy,g.x+x2*g.sx,g.y+y2*g.sy,
                 ObjectColor(strokeColor,g.alpha).ToAllegro(),strokeWidth*std::min(g.sx,g.sy));
    UIObject::Draw();
}
UITextPoint::UITextPoint() : font("AeroMaticsRegular"),fontSize(18),leading(0),fillColor("FFFFFF"),
    align(TextAlign::Left),cachedFont(nullptr),cachedSize(0),cachedGeneration(0) {}
ALLEGRO_FONT* UITextPoint::GetFont() {
    const unsigned generation=Allegro5System::FontGeneration();
    if (cachedName!=font || cachedBase!=assetBasePath || cachedSize!=fontSize || cachedGeneration!=generation) {
        cachedName=font; cachedBase=assetBasePath; cachedSize=fontSize; cachedGeneration=generation;
        std::string path=font;
        if (path.find('/')==std::string::npos && path.find('\\')==std::string::npos) {
            if (path.find('.')==std::string::npos) path+=".ttf";
            if (!assetBasePath.empty()) path=assetBasePath+"/"+path;
        }
        cachedFont=Allegro5System::LoadFont(path.c_str(),fontSize);
    }
    return cachedFont;
}
void UITextPoint::Update(float dt) { PrepareText(); UIObject::Update(dt); }
void UITextPoint::PrepareText() { GetFont(); }
float UITextPoint::Measure(size_t begin,size_t end) {
    ALLEGRO_FONT* font=GetFont();
    if (!font) return 0;
    begin=std::min(begin,text.size()); end=std::max(begin,std::min(end,text.size()));
    ALLEGRO_USTR_INFO info;
    return al_get_ustr_width(font,al_ref_buffer(&info,text.data()+begin,end-begin));
}
void UITextPoint::DrawRange(size_t begin,size_t end,float px,float py) {
    ALLEGRO_FONT* font=GetFont();
    if (!font || begin>=end) return;
    const Geometry geometry=GetGeometry();
    ALLEGRO_TRANSFORM saved=*al_get_current_transform(),transform;
    al_identity_transform(&transform);
    al_scale_transform(&transform,geometry.sx,geometry.sy);
    al_translate_transform(&transform,geometry.x,geometry.y);
    al_compose_transform(&transform,&saved);
    al_use_transform(&transform);
    ALLEGRO_USTR_INFO info;
    al_draw_ustr(font,ObjectColor(fillColor,geometry.alpha).ToAllegro(),px,py,0,
                 al_ref_buffer(&info,text.data()+begin,end-begin));
    al_use_transform(&saved);
}
void UITextPoint::Draw() {
    if (!IsVisible()) return;
    PrepareText();
    const float width=align==TextAlign::Left ? 0 : Measure(0,text.size());
    const float offset=align==TextAlign::Center ? -width*0.5f : align==TextAlign::Right ? -width : 0;
    DrawRange(0,text.size(),offset,0);
    UIObject::Draw();
}
size_t UITextPoint::CaretAt(float px,float) {
    const Geometry geometry=GetGeometry();
    if (geometry.sx==0) return 0;
    const float width=Measure(0,text.size());
    const float offset=align==TextAlign::Center ? -width*0.5f : align==TextAlign::Right ? -width : 0;
    const float local=(px-geometry.x)/geometry.sx-offset;
    float previous=0;
    for (size_t i=0;i<text.size();) {
        const size_t next=NextCharacter(text,i);
        const float current=Measure(0,next);
        if (local<(previous+current)*0.5f) return i;
        previous=current; i=next;
    }
    return text.size();
}
void UITextPoint::CaretPosition(size_t index,float& px,float& py) {
    const float width=Measure(0,text.size());
    px=Measure(0,index)+(align==TextAlign::Center ? -width*0.5f : align==TextAlign::Right ? -width : 0);
    py=0;
}
UITextParagraph::UITextParagraph() : wordWrap(true),wrappedWidth(-1),wrappedFont(nullptr),wrappedMode(false) {}
void UITextParagraph::PrepareText() {
    ALLEGRO_FONT* font=GetFont();
    if (wrappedText==text && wrappedWidth==w && wrappedFont==font && wrappedMode==wordWrap) return;
    wrappedText=text; wrappedWidth=w; wrappedFont=font; wrappedMode=wordWrap;
    lines.clear();
    size_t begin=0;
    while (begin<text.size()) {
        size_t end=begin,lastSpace=std::string::npos;
        while (end<text.size() && text[end]!='\n' && text[end]!='\r') {
            const size_t next=NextCharacter(text,end);
            if (wordWrap && w>0 && end>begin && Measure(begin,next)>w) {
                if (lastSpace!=std::string::npos && lastSpace>begin) end=lastSpace;
                break;
            }
            if (WordSpace(text,end)) lastSpace=end;
            end=next;
        }
        if (end==begin && end<text.size() && text[end]!='\n' && text[end]!='\r') end=NextCharacter(text,end);
        lines.push_back(TextLine{begin,end,Measure(begin,end)});
        begin=end;
        if (begin<text.size() && (text[begin]=='\n' || text[begin]=='\r')) {
            const char newline=text[begin++];
            if (newline=='\r' && begin<text.size() && text[begin]=='\n') ++begin;
        } else {
            while (begin<text.size() && (text[begin]==' ' || text[begin]=='\t')) ++begin;
        }
    }
    if (text.empty() || text.back()=='\n' || text.back()=='\r') lines.push_back(TextLine{text.size(),text.size(),0});
}
void UITextParagraph::Draw() {
    if (!IsVisible()) return;
    PrepareText();
    ALLEGRO_FONT* font=GetFont();
    if (font) {
        const float advance=std::max(1.0f,static_cast<float>(al_get_font_line_height(font))+leading);
        float py=0;
        for (const TextLine& line : lines) {
            if (h>0 && py>=h) break;
            DrawRange(line.begin,line.end,LineOffset(align,line.width,w),py);
            py+=advance;
        }
    }
    UIObject::Draw();
}
size_t UITextParagraph::CaretAt(float px,float py) {
    PrepareText();
    ALLEGRO_FONT* font=GetFont();
    if (!font || lines.empty()) return 0;
    const Geometry geometry=GetGeometry();
    if (!geometry.sx || !geometry.sy) return 0;
    const float advance=std::max(1.0f,static_cast<float>(al_get_font_line_height(font))+leading);
    const size_t row=static_cast<size_t>(Clamp(std::floor((py-geometry.y)/geometry.sy/advance),0,static_cast<float>(lines.size()-1)));
    const TextLine& line=lines[row];
    const float local=(px-geometry.x)/geometry.sx-LineOffset(align,line.width,w);
    float previous=0;
    for (size_t i=line.begin;i<line.end;) {
        const size_t next=NextCharacter(text,i);
        const float current=Measure(line.begin,next);
        if (local<(previous+current)*0.5f) return i;
        previous=current; i=next;
    }
    return line.end;
}
void UITextParagraph::CaretPosition(size_t index,float& px,float& py) {
    PrepareText(); px=0; py=0;
    ALLEGRO_FONT* font=GetFont();
    if (!font) return;
    const float advance=std::max(1.0f,static_cast<float>(al_get_font_line_height(font))+leading);
    for (size_t row=0;row<lines.size();++row) {
        const TextLine& line=lines[row];
        const bool continues=row+1<lines.size() && lines[row+1].begin==line.end && index==line.end;
        if ((index<=line.end && !continues) || row+1==lines.size()) {
            px=LineOffset(align,line.width,w)+Measure(line.begin,std::min(index,line.end));
            py=static_cast<float>(row)*advance; return;
        }
    }
}
UIImage::UIImage() : bitmap(nullptr),ownsbitmap(false) {}
UIImage::~UIImage() { if (ownsbitmap && bitmap && bitmap!=texture.get()) al_destroy_bitmap(static_cast<ALLEGRO_BITMAP*>(bitmap)); }
void UIImage::LoadBitmap(const std::string& basePath) {
    if (ownsbitmap && bitmap && bitmap!=texture.get()) al_destroy_bitmap(static_cast<ALLEGRO_BITMAP*>(bitmap));
    bitmap=nullptr; ownsbitmap=false; texture.reset();
    texture=AtlasManager::GetInstance().AcquireTexture(file);
    if (!texture) {
        const std::string path=file.empty() || file[0]=='/' || basePath.empty() ? file : basePath+"/"+file;
        ALLEGRO_BITMAP* loaded=Allegro5System::LoadBitmap(path.c_str());
        if (loaded) texture=std::shared_ptr<ALLEGRO_BITMAP>(loaded,al_destroy_bitmap);
    }
    bitmap=texture.get();
    if (bitmap) {
        if (w<=0) w=static_cast<float>(al_get_bitmap_width(texture.get()));
        if (h<=0) h=static_cast<float>(al_get_bitmap_height(texture.get()));
    }
}
void UIImage::Draw() {
    if (!IsVisible()) return;
    if (bitmap) {
        const Geometry g=GetGeometry();
        ALLEGRO_BITMAP* image=static_cast<ALLEGRO_BITMAP*>(bitmap);
        al_draw_tinted_scaled_bitmap(image,al_map_rgba_f(1,1,1,g.alpha),0,0,al_get_bitmap_width(image),
            al_get_bitmap_height(image),g.x,g.y,g.w,g.h,0);
    }
    UIObject::Draw();
}
void UIPoly::PrepareVertices() {
    transformed.resize(vertices.size()*2);
    preparedVertices=vertices;
    triangleIndices.clear();
    std::vector<int> remaining;
    remaining.reserve(vertices.size());
    float area=0;
    for (size_t i=0;i<vertices.size();++i) {
        const Vertex& a=vertices[i]; const Vertex& b=vertices[(i+1)%vertices.size()];
        area+=a.x*b.y-b.x*a.y;
        remaining.push_back(static_cast<int>(i));
    }
    const float orientation=area>=0 ? 1.0f : -1.0f;
    const auto cross=[](const Vertex& a,const Vertex& b,const Vertex& c) {
        return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
    };
    while (remaining.size()>3) {
        bool removed=false;
        for (size_t i=0;i<remaining.size();++i) {
            const int a=remaining[(i+remaining.size()-1)%remaining.size()];
            const int b=remaining[i],c=remaining[(i+1)%remaining.size()];
            const float turn=cross(vertices[a],vertices[b],vertices[c])*orientation;
            if (std::fabs(turn)<0.00001f) {
                remaining.erase(remaining.begin()+i); removed=true; break;
            }
            if (turn<0) continue;
            bool occupied=false;
            for (int point : remaining) if (point!=a && point!=b && point!=c &&
                cross(vertices[a],vertices[b],vertices[point])*orientation>=0 &&
                cross(vertices[b],vertices[c],vertices[point])*orientation>=0 &&
                cross(vertices[c],vertices[a],vertices[point])*orientation>=0) { occupied=true; break; }
            if (occupied) continue;
            triangleIndices.push_back(a); triangleIndices.push_back(b); triangleIndices.push_back(c);
            remaining.erase(remaining.begin()+i); removed=true; break;
        }
        if (!removed) { triangleIndices.clear(); remaining.clear(); break; }
    }
    if (remaining.size()==3) triangleIndices.insert(triangleIndices.end(),remaining.begin(),remaining.end());
    triangleVertices.resize(triangleIndices.size());
}
void UIPoly::Draw() {
    if (!IsVisible()) return;
    bool changed=preparedVertices.size()!=vertices.size();
    for (size_t i=0;!changed && i<vertices.size();++i)
        changed=preparedVertices[i].x!=vertices[i].x || preparedVertices[i].y!=vertices[i].y;
    if (changed) PrepareVertices();
    const Geometry g=GetGeometry();
    for (size_t i=0;i<vertices.size();++i) { transformed[i*2]=g.x+vertices[i].x*g.sx; transformed[i*2+1]=g.y+vertices[i].y*g.sy; }
    if (Filled(subtype) && !triangleIndices.empty()) {
        const ALLEGRO_COLOR fill=ObjectColor(fillColor,g.alpha*fillAlpha).ToAllegro();
        for (size_t i=0;i<triangleIndices.size();++i) {
            ALLEGRO_VERTEX& vertex=triangleVertices[i];
            vertex.x=transformed[triangleIndices[i]*2]; vertex.y=transformed[triangleIndices[i]*2+1];
            vertex.z=0; vertex.u=0; vertex.v=0; vertex.color=fill;
        }
        al_draw_prim(triangleVertices.data(),nullptr,nullptr,0,static_cast<int>(triangleVertices.size()),ALLEGRO_PRIM_TRIANGLE_LIST);
    }
    if (vertices.size()>=2 && Stroked(subtype) && strokeWidth>0) {
        const ALLEGRO_COLOR stroke=ObjectColor(strokeColor,g.alpha).ToAllegro();
        for (size_t i=0;i<vertices.size();++i) {
            const size_t next=(i+1)%vertices.size();
            al_draw_line(transformed[i*2],transformed[i*2+1],transformed[next*2],transformed[next*2+1],
                stroke,strokeWidth*std::min(g.sx,g.sy));
        }
    }
    UIObject::Draw();
}
void UICircle::Draw() {
    if (!IsVisible()) return;
    const Geometry g=GetGeometry(); const float cx=g.x+g.w*0.5f,cy=g.y+g.h*0.5f;
    if (Filled(subtype)) al_draw_filled_ellipse(cx,cy,g.w*0.5f,g.h*0.5f,ObjectColor(fillColor,g.alpha*fillAlpha).ToAllegro());
    if (Stroked(subtype) && strokeWidth>0) al_draw_ellipse(cx,cy,g.w*0.5f,g.h*0.5f,ObjectColor(strokeColor,g.alpha).ToAllegro(),strokeWidth*std::min(g.sx,g.sy));
    UIObject::Draw();
}
UIArc::UIArc() : startAngle(-90),endAngle(270) {}
void UIArc::Draw() {
    if (!IsVisible()) return;
    const Geometry g=GetGeometry(); const float radians=static_cast<float>(ALLEGRO_PI/180.0);
    al_draw_elliptical_arc(g.x+g.w*0.5f,g.y+g.h*0.5f,g.w*0.5f,g.h*0.5f,startAngle*radians,
        (endAngle-startAngle)*radians,ObjectColor(strokeColor,g.alpha).ToAllegro(),strokeWidth*std::min(g.sx,g.sy));
    UIObject::Draw();
}
UIDraggable::UIDraggable() : dragHorizontal(true),dragVertical(true),bounded(false),minX(0),minY(0),maxX(0),maxY(0),dragX(0),dragY(0),startX(0),startY(0) {}
void UIDraggable::OnPointerDown(float px,float py,int button) {
    UIInteractive::OnPointerDown(px,py,button);
    if (button==1) { dragX=px; dragY=py; startX=x; startY=y; }
}
void UIDraggable::OnPointerMove(float px,float py) {
    if (!isPressed) return;
    const Geometry inherited=parent ? parent->GetGeometry() : Geometry{0,0,0,0,Allegro5System::ScaleW(1),Allegro5System::ScaleH(1),1};
    if (dragHorizontal && inherited.sx) x=startX+(px-dragX)/inherited.sx;
    if (dragVertical && inherited.sy) y=startY+(py-dragY)/inherited.sy;
    if (bounded) { x=Clamp(x,minX,std::max(minX,maxX-w)); y=Clamp(y,minY,std::max(minY,maxY-h)); }
}
UIButtonStandard::UIButtonStandard() : gfxStandard(nullptr),gfxHover(nullptr),gfxClick(nullptr),focusedState(false) { focusable=true; }
void UIButtonStandard::UpdateStateGraphics() {
    // Re-resolve borrowed state children after any tree mutation.
    gfxStandard=FindChild("gfxStandard"); gfxHover=FindChild("gfxHover"); gfxClick=FindChild("gfxClick");
    UIObject* disabled=FindChild("gfxDisabled");
    UIObject* focus=FindChild("gfxFocus");
    const bool active=IsEnabled();
    UIObject* state=active && isPressed && gfxClick ? gfxClick : active && isHovered && gfxHover ? gfxHover : gfxStandard;
    if (!active && disabled) state=disabled;
    if (gfxStandard) gfxStandard->visible=state==gfxStandard;
    if (gfxHover) gfxHover->visible=state==gfxHover;
    if (gfxClick) gfxClick->visible=state==gfxClick;
    if (disabled) disabled->visible=state==disabled;
    if (focus) focus->visible=active && focusedState;
}
void UIButtonStandard::Draw() { if (IsVisible()) { UpdateStateGraphics(); UIObject::Draw(); } }
void UIButtonStandard::OnMouseEnter() { isHovered=true; UpdateStateGraphics(); UIObject::OnMouseEnter(); }
void UIButtonStandard::OnMouseLeave() { UIObject::OnMouseLeave(); UpdateStateGraphics(); }
void UIButtonStandard::OnMouseDown() { UIObject::OnMouseDown(); UpdateStateGraphics(); }
void UIButtonStandard::OnMouseUp() {
    const bool click=isPressed && isHovered && IsEnabled(); isPressed=false; UpdateStateGraphics();
    if (click) OnClick();
}
void UIButtonStandard::OnFocusChanged(bool focused) { focusedState=focused; UpdateStateGraphics(); }
bool UIButtonStandard::OnKey(int key,unsigned,bool down) {
    if (!IsEnabled() || (key!=ALLEGRO_KEY_ENTER && key!=ALLEGRO_KEY_PAD_ENTER && key!=ALLEGRO_KEY_SPACE)) return false;
    if (down) { isPressed=false; UpdateStateGraphics(); OnClick(); }
    return true;
}
void UIButtonSelect::UpdateStateGraphics() {
    UIButtonStandard::UpdateStateGraphics();
    const bool active=IsEnabled();
    const char* standard=selected ? "gfxStandardOn" : "gfxStandardOff";
    const char* hover=selected ? "gfxHoverOn" : "gfxHoverOff";
    const char* click=selected ? "gfxClickOn" : "gfxClickOff";
    UIObject* base=FindChild(standard),*over=FindChild(hover),*pressed=FindChild(click);
    UIObject* current=active && isPressed && pressed ? pressed : active && isHovered && over ? over : base;
    for (auto& child : children) if (child && (child->name=="gfxStandardOn" || child->name=="gfxStandardOff" ||
        child->name=="gfxHoverOn" || child->name=="gfxHoverOff" || child->name=="gfxClickOn" || child->name=="gfxClickOff")) child->visible=child.get()==current;
}
void UIButtonSelect::OnClick() { if (!IsEnabled()) return; selected=!selected; UpdateStateGraphics(); UIObject::OnClick(); }
UIButtonTextField::UIButtonTextField() : isPassword(false),maxLength(256),hasFocus(false),caret(0),multiline(false),
    initializedText(false),displayedPassword(false),displayedFocus(false),blinkTime(0),viewOffsetX(0),viewOffsetY(0) { clipChildren=true; }
UITextPoint* UIButtonTextField::Caption() {
    return dynamic_cast<UITextPoint*>(FindChild("txtCaption"));
}
void UIButtonTextField::SyncCaption() {
    UITextPoint* caption=Caption();
    if (!caption) return;
    const bool first=!initializedText;
    if (first) { promptText=caption->text; initializedText=true; }
    if (first || displayedText!=inputText || displayedPassword!=isPassword || displayedFocus!=hasFocus) {
        if (inputText.empty() && !hasFocus) caption->text=promptText;
        else if (isPassword) caption->text.assign(CharacterCount(inputText),'*');
        else caption->text=inputText;
        if (displayedText!=inputText) displayedText=inputText;
        displayedPassword=isPassword; displayedFocus=hasFocus;
    }
    caption->PrepareText();
    if (multiline) {
        if (auto* paragraph=dynamic_cast<UITextParagraph*>(caption)) {
            ALLEGRO_FONT* font=paragraph->GetFont();
            const float advance=std::max(1.0f,
                (font ? static_cast<float>(al_get_font_line_height(font))
                      : static_cast<float>(paragraph->fontSize))+paragraph->leading);
            paragraph->h=std::max(0.0f,std::max(h-paragraph->y,
                advance*static_cast<float>(paragraph->lines.size())));
        }
    }
}
void UIButtonTextField::SetText(const std::string& value) {
    inputText=value;
    if (maxLength>=0) {
        size_t end=0; int count=0;
        while (end<inputText.size() && count<maxLength) { end=NextCharacter(inputText,end); ++count; }
        inputText.resize(end);
    }
    caret=inputText.size(); blinkTime=0; SyncCaption(); KeepCaretVisible();
}
void UIButtonTextField::KeepCaretVisible() {
    UITextPoint* caption=Caption();
    if (!caption) return;
    size_t index=caret;
    if (isPassword) { index=0; for (size_t i=0;i<caret;i=NextCharacter(inputText,i)) ++index; }
    float px=0,py=0; caption->CaretPosition(index,px,py);
    const float height=caption->GetFont() ? static_cast<float>(al_get_font_line_height(caption->GetFont())) : static_cast<float>(caption->fontSize);
    const float right=std::max(0.0f,w-caption->x-2);
    if (caption->x+px-viewOffsetX>w-2) viewOffsetX=caption->x+px-w+2;
    if (px-viewOffsetX<0) viewOffsetX=px;
    if (caption->y+py+height-viewOffsetY>h) viewOffsetY=caption->y+py+height-h;
    if (py-viewOffsetY<0) viewOffsetY=py;
    if (right<=0) viewOffsetX=0;
    viewOffsetX=std::max(0.0f,viewOffsetX); viewOffsetY=std::max(0.0f,viewOffsetY);
}
void UIButtonTextField::Update(float dt) {
    caret=std::min(caret,inputText.size());
    blinkTime=std::fmod(blinkTime+std::max(0.0f,dt),1.0f);
    SyncCaption(); UIObject::Update(dt);
}
void UIButtonTextField::OnFocusChanged(bool focused) {
    hasFocus=focused; blinkTime=0;
    UIButtonStandard::OnFocusChanged(focused); SyncCaption(); KeepCaretVisible();
}
void UIButtonTextField::OnClick() { SetFocus(this); UIObject::OnClick(); }
void UIButtonTextField::OnPointerDown(float px,float py,int button) {
    UIButtonStandard::OnPointerDown(px,py,button);
    if (button!=1) return;
    SetFocus(this); SyncCaption();
    UITextPoint* caption=Caption();
    if (caption) {
        const Geometry geometry=caption->GetGeometry();
        size_t index=caption->CaretAt(px+viewOffsetX*geometry.sx,py+viewOffsetY*geometry.sy);
        if (isPassword) {
            caret=0;
            while (index-- && caret<inputText.size()) caret=NextCharacter(inputText,caret);
        } else caret=std::min(index,inputText.size());
    } else caret=inputText.size();
    blinkTime=0; KeepCaretVisible();
}
size_t UIButtonTextField::PreviousWord(size_t from) const {
    if (!from) return 0;
    if (from==inputText.size() || WordSpace(inputText,from)) from=PreviousCharacter(inputText,from);
    while (from && !WordSpace(inputText,from)) from=PreviousCharacter(inputText,from);
    return from;
}
size_t UIButtonTextField::NextWord(size_t from) const {
    if (WordSpace(inputText,from)) from=NextCharacter(inputText,from);
    while (from<inputText.size() && !WordSpace(inputText,from)) from=NextCharacter(inputText,from);
    return from;
}
bool UIButtonTextField::OnTextInput(int codepoint) {
    if (codepoint=='\b') return OnKey(ALLEGRO_KEY_BACKSPACE,0,true);
    if (!hasFocus || !IsEnabled() || codepoint<32 || (codepoint>=0x7f && codepoint<=0x9f) ||
        codepoint>0x10ffff || (codepoint>=0xd800 && codepoint<=0xdfff)) return false;
    if (maxLength>=0 && CharacterCount(inputText)>=static_cast<size_t>(maxLength)) return true;
    char encoded[4]; int length=al_utf8_encode(encoded,codepoint);
    if (length>0) { caret=std::min(caret,inputText.size()); inputText.insert(caret,encoded,length); caret+=length; }
    blinkTime=0; SyncCaption(); KeepCaretVisible(); return true;
}
bool UIButtonTextField::OnKey(int key,unsigned modifiers,bool down) {
    if (!hasFocus || !IsEnabled()) return false;
    if (!down) return key==ALLEGRO_KEY_ENTER || key==ALLEGRO_KEY_PAD_ENTER;
    const bool control=(modifiers & ALLEGRO_KEYMOD_CTRL)!=0;
    caret=std::min(caret,inputText.size());
    switch (key) {
        case ALLEGRO_KEY_LEFT: caret=control ? PreviousWord(caret) : PreviousCharacter(inputText,caret); break;
        case ALLEGRO_KEY_RIGHT: caret=control ? NextWord(caret) : NextCharacter(inputText,caret); break;
        case ALLEGRO_KEY_HOME: caret=0; break;
        case ALLEGRO_KEY_END: caret=inputText.size(); break;
        case ALLEGRO_KEY_BACKSPACE: {
            const size_t begin=control ? PreviousWord(caret) : PreviousCharacter(inputText,caret);
            inputText.erase(begin,caret-begin); caret=begin; break;
        }
        case ALLEGRO_KEY_DELETE: {
            size_t end=control ? NextWord(caret) : NextCharacter(inputText,caret);
            if (control && end<inputText.size()) end=NextCharacter(inputText,end);
            inputText.erase(caret,end-caret); break;
        }
        case ALLEGRO_KEY_ENTER:
        case ALLEGRO_KEY_PAD_ENTER:
            if (multiline && !control) {
                if (maxLength<0 || CharacterCount(inputText)<static_cast<size_t>(maxLength)) inputText.insert(caret++,1,'\n');
            } else {
                blinkTime=0;
                if (onEnterCallback) onEnterCallback(this);
                else UIObject::OnClick();
                return true;
            }
            break;
        case ALLEGRO_KEY_UP:
        case ALLEGRO_KEY_DOWN: {
            if (!multiline) return false;
            UITextPoint* caption=Caption();
            if (!caption) return true;
            float px,py; caption->CaretPosition(caret,px,py);
            const Geometry g=caption->GetGeometry();
            const float height=caption->GetFont() ? static_cast<float>(al_get_font_line_height(caption->GetFont()))+caption->leading : caption->fontSize;
            caret=caption->CaretAt(g.x+px*g.sx,g.y+(py+(key==ALLEGRO_KEY_UP ? -height : height)+height*0.5f)*g.sy);
            break;
        }
        default: return false;
    }
    blinkTime=0; SyncCaption(); KeepCaretVisible(); return true;
}
void UIButtonTextField::Draw() {
    if (!IsVisible()) return;
    SyncCaption(); UpdateStateGraphics();
    Clip clip(GetGeometry());
    UITextPoint* caption=Caption();
    for (auto& child : children) if (child && child->visible) {
        if (child.get()==caption) {
            const float savedX=child->x,savedY=child->y;
            child->x-=viewOffsetX; child->y-=viewOffsetY;
            child->Draw(); child->x=savedX; child->y=savedY;
        } else child->Draw();
    }
    if (hasFocus && IsEnabled() && blinkTime<0.5f && caption) {
        size_t index=caret;
        if (isPassword) { index=0; for (size_t i=0;i<caret;i=NextCharacter(inputText,i)) ++index; }
        float px=0,py=0; caption->CaretPosition(index,px,py);
        const Geometry g=caption->GetGeometry();
        const float height=caption->GetFont() ? static_cast<float>(al_get_font_line_height(caption->GetFont())) : caption->fontSize;
        const float cx=g.x+(px-viewOffsetX)*g.sx,cy=g.y+(py-viewOffsetY)*g.sy;
        al_draw_line(cx,cy,cx,cy+height*g.sy,ObjectColor(caption->fillColor,g.alpha).ToAllegro(),std::max(1.0f,g.sx));
    }
}
// The authored bars are overlays owned outside the content vector: they never
// affect arrangement/content bounds or move with ChildOffsetX/Y. Their complete
// subtrees still have this list as an ancestor for input invalidation.
class UIScrollableList::Scrollbar : public UIInteractive {
public:
    Scrollbar(UIScrollableList& owner,bool horizontal,std::unique_ptr<UIObject> layout)
        : owner(owner),horizontal(horizontal),dragging(false),pointerStart(0),scrollStart(0),
          track(nullptr),handle(nullptr),decrease(nullptr),increase(nullptr) {
        name=horizontal ? "ScrollbarH" : "ScrollbarV";
        MakePassive(*layout);
        track=layout->FindChild("btnBase");
        handle=layout->FindChild("handle");
        decrease=layout->FindChild(horizontal ? "btnDown" : "btnUp");
        increase=layout->FindChild(horizontal ? "btnUp" : "btnDown");
        AddChild(std::move(layout));
        parent=&owner;
    }
    bool IsComplete() const { return track && handle && decrease && increase && handle->FindChild("gfx"); }
    void Synchronize(float length,float viewport,float maximum,float offset) {
        w=horizontal ? length : 12;
        h=horizontal ? 12 : length;
        // Compensate the content offset, without changing the content tree.
        x=owner.scrollX+(horizontal ? 0 : owner.viewportWidth);
        y=owner.scrollY+(horizontal ? owner.viewportHeight : 0);
        UIObject& layout=*children.front();
        layout.w=w; layout.h=h;
        const float trackLength=std::max(0.0f,length-24);
        const float thumbLength=std::min(trackLength,std::max(12.0f,
            trackLength*viewport/std::max(viewport,viewport+maximum)));
        const float position=12+(maximum>0 ? offset/maximum*(trackLength-thumbLength) : 0);
        SetAxis(*track,12,trackLength);
        for (auto& graphic : track->children) if (graphic) SetAxis(*graphic,0,trackLength);
        SetAxis(*decrease,0,12);
        SetAxis(*increase,std::max(0.0f,length-12),12);
        SetAxis(*handle,position,thumbLength);
        SetAxis(*handle->FindChild("gfx"),0,thumbLength);
    }
    void OnPointerDown(float px,float py,int button) override {
        UIInteractive::OnPointerDown(px,py,button);
        if (button!=1 || !IsEnabled()) return;
        dragging=handle->ContainsPoint(px,py);
        pointerStart=horizontal ? px : py;
        scrollStart=horizontal ? owner.scrollX : owner.scrollY;
        if (decrease->ContainsPoint(px,py)) ScrollBy(-owner.wheelStep);
        else if (increase->ContainsPoint(px,py)) ScrollBy(owner.wheelStep);
        else if (!dragging && track->ContainsPoint(px,py)) {
            const Geometry thumb=handle->GetGeometry();
            const bool before=horizontal ? px<thumb.x : py<thumb.y;
            ScrollBy((before ? -1 : 1)*(horizontal ? owner.viewportWidth : owner.viewportHeight));
            // A held track press can continue as a drag after its page step.
            scrollStart=horizontal ? owner.scrollX : owner.scrollY;
            dragging=true;
        }
        UpdateVisuals(px,py);
    }
    void OnPointerMove(float px,float py) override {
        if (isPressed && dragging) {
            const Geometry geometry=GetGeometry();
            const float scale=horizontal ? geometry.sx : geometry.sy;
            const float travel=horizontal ? track->w-handle->w : track->h-handle->h;
            const float maximum=horizontal ? owner.maxScrollX : owner.maxScrollY;
            if (scale>0 && travel>0) {
                const float offset=scrollStart+((horizontal ? px : py)-pointerStart)*maximum/(scale*travel);
                owner.SetScroll(horizontal ? offset : owner.scrollX,horizontal ? owner.scrollY : offset);
            }
        }
        UpdateVisuals(px,py);
    }
    void OnPointerUp(float px,float py,int button) override {
        if (button!=1) return;
        isPressed=false; dragging=false;
        UpdateVisuals(px,py);
    }
    void OnMouseLeave() override {
        UIInteractive::OnMouseLeave();
        for (UIObject* control : {track,decrease,increase}) control->isHovered=false;
    }
private:
    UIScrollableList& owner;
    bool horizontal,dragging;
    float pointerStart,scrollStart;
    UIObject *track,*handle,*decrease,*increase;
    static void MakePassive(UIObject& object) {
        object.interactive=false;
        object.focusable=false;
        for (auto& child : object.children) if (child) MakePassive(*child);
    }
    void SetAxis(UIObject& object,float position,float length) {
        if (horizontal) { object.x=position; object.w=length; }
        else { object.y=position; object.h=length; }
    }
    void ScrollBy(float amount) {
        owner.SetScroll(owner.scrollX+(horizontal ? amount : 0),owner.scrollY+(horizontal ? 0 : amount));
    }
    void UpdateVisuals(float px,float py) {
        for (UIObject* control : {track,decrease,increase}) {
            control->isHovered=isHovered && control->ContainsPoint(px,py);
            control->isPressed=isPressed && !dragging && control->isHovered;
        }
    }
};

UIScrollableList::UIScrollableList() : scrollX(0),scrollY(0),wheelStep(30),scrollHorizontal(false),scrollVertical(true),
    dragScroll(true),dragX(0),dragY(0),startX(0),startY(0),triedHorizontalScrollbar(false),triedVerticalScrollbar(false),
    viewportWidth(0),viewportHeight(0),maxScrollX(0),maxScrollY(0) { clipChildren=true; }
UIScrollableList::~UIScrollableList() = default;
void UIScrollableList::ClampScroll() {
    const Geometry own=GetGeometry();
    float right=own.x-scrollX*own.sx,bottom=own.y-scrollY*own.sy;
    for (auto& child : children) if (child) ContentBounds(*child,right,bottom);
    const float width=own.sx ? (right-own.x)/own.sx+scrollX : 0;
    const float height=own.sy ? (bottom-own.y)/own.sy+scrollY : 0;
    bool horizontal=scrollHorizontal && width>w && w>=36 && h>=12;
    bool vertical=scrollVertical && height>h && h>=36 && w>=12;
    // Either bar can make the other axis overflow.
    horizontal=scrollHorizontal && width>w-(vertical ? 12 : 0) && w>=36 && h>=12;
    vertical=scrollVertical && height>h-(horizontal ? 12 : 0) && h>=36 && w>=12;
    horizontal=scrollHorizontal && width>w-(vertical ? 12 : 0) && w>=36 && h>=12;
    if (horizontal && !horizontalScrollbar && !triedHorizontalScrollbar) {
        triedHorizontalScrollbar=true;
        auto layout=LayoutManager::GetInstance().TakeLayout("Misc/ScrollbarH");
        if (layout) {
            std::unique_ptr<Scrollbar> bar(new Scrollbar(*this,true,std::move(layout)));
            if (bar->IsComplete()) horizontalScrollbar=std::move(bar);
        }
    }
    if (vertical && !verticalScrollbar && !triedVerticalScrollbar) {
        triedVerticalScrollbar=true;
        auto layout=LayoutManager::GetInstance().TakeLayout("Misc/ScrollbarV");
        if (layout) {
            std::unique_ptr<Scrollbar> bar(new Scrollbar(*this,false,std::move(layout)));
            if (bar->IsComplete()) verticalScrollbar=std::move(bar);
        }
    }
    horizontal=horizontal && horizontalScrollbar;
    vertical=vertical && verticalScrollbar;
    viewportWidth=std::max(0.0f,w-(vertical ? 12 : 0));
    viewportHeight=std::max(0.0f,h-(horizontal ? 12 : 0));
    maxScrollX=scrollHorizontal ? std::max(0.0f,width-viewportWidth) : 0;
    maxScrollY=scrollVertical ? std::max(0.0f,height-viewportHeight) : 0;
    scrollX=Clamp(scrollX,0,maxScrollX);
    scrollY=Clamp(scrollY,0,maxScrollY);
    if (horizontalScrollbar) {
        horizontalScrollbar->visible=horizontal;
        horizontalScrollbar->Synchronize(viewportWidth,viewportWidth,maxScrollX,scrollX);
    }
    if (verticalScrollbar) {
        verticalScrollbar->visible=vertical;
        verticalScrollbar->Synchronize(viewportHeight,viewportHeight,maxScrollY,scrollY);
    }
}
void UIScrollableList::SetScroll(float sx,float sy) { scrollX=sx; scrollY=sy; ArrangeChildren(); ClampScroll(); }
void UIScrollableList::Update(float dt) {
    UIObject::Update(dt); ClampScroll();
    if (horizontalScrollbar) horizontalScrollbar->Update(dt);
    if (verticalScrollbar) verticalScrollbar->Update(dt);
}
void UIScrollableList::Draw() {
    if (!IsVisible()) return;
    ClampScroll();
    const Geometry own=GetGeometry();
    {
        Geometry viewport=own;
        viewport.w=viewportWidth*own.sx; viewport.h=viewportHeight*own.sy;
        Clip clip(viewport);
        UIObject::Draw();
    }
    Clip clip(own);
    if (horizontalScrollbar) horizontalScrollbar->Draw();
    if (verticalScrollbar) verticalScrollbar->Draw();
}
UIObject* UIScrollableList::HitTest(float px,float py) {
    if (!IsVisible() || !IsEnabled() || !ContainsPoint(px,py)) return nullptr;
    ClampScroll();
    if (verticalScrollbar) if (UIObject* hit=verticalScrollbar->HitTest(px,py)) return hit;
    if (horizontalScrollbar) if (UIObject* hit=horizontalScrollbar->HitTest(px,py)) return hit;
    const Geometry own=GetGeometry();
    if (px>=own.x+viewportWidth*own.sx || py>=own.y+viewportHeight*own.sy) return this;
    return UIInteractive::HitTest(px,py);
}
void UIScrollableList::OnPointerDown(float px,float py,int button) {
    UIInteractive::OnPointerDown(px,py,button);
    if (button==1) { dragX=px; dragY=py; startX=scrollX; startY=scrollY; }
}
void UIScrollableList::OnPointerMove(float px,float py) {
    if (!dragScroll || !isPressed) return;
    const Geometry g=GetGeometry();
    SetScroll(scrollHorizontal && g.sx ? startX-(px-dragX)/g.sx : scrollX,
              scrollVertical && g.sy ? startY-(py-dragY)/g.sy : scrollY);
}
bool UIScrollableList::OnMouseWheel(float dx,float dy) {
    if (!IsEnabled()) return false;
    SetScroll(scrollX-(scrollHorizontal ? (dx ? dx : !scrollVertical ? dy : 0)*wheelStep : 0),
              scrollY-(scrollVertical ? dy*wheelStep : 0));
    return scrollHorizontal || scrollVertical;
}
} // namespace HDUI
