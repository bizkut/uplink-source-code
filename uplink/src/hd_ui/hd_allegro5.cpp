#include "hd_allegro5.h"
#include "hd_ui_object.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace HDUI {
namespace {
struct FontEntry {
    std::string name;
    int size;
    ALLEGRO_FONT* font;
};
std::vector<FontEntry> fonts;
unsigned fontGeneration = 1;

bool InitializeAddons() {
    if (!al_is_system_installed()) return false;
    if (!al_is_primitives_addon_initialized() && !al_init_primitives_addon()) return false;
    if (!al_is_font_addon_initialized()) al_init_font_addon();
    if (!al_is_ttf_addon_initialized() && !al_init_ttf_addon()) return false;
    if (!al_is_image_addon_initialized() && !al_init_image_addon()) return false;
    return al_is_font_addon_initialized();
}
int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
}

ALLEGRO_DISPLAY* Allegro5System::display = nullptr;
UIObject* Allegro5System::inputRoot = nullptr;
int Allegro5System::screenWidth = 1920;
int Allegro5System::screenHeight = 1080;
int Allegro5System::baseWidth = 1920;
int Allegro5System::baseHeight = 1080;
float Allegro5System::scaleX = 1;
float Allegro5System::scaleY = 1;
bool Allegro5System::initialized = false;

bool Allegro5System::Initialize(int, int, bool) {
    return AttachDisplay(al_get_current_display());
}
bool Allegro5System::AttachDisplay(ALLEGRO_DISPLAY* borrowedDisplay) {
    if (!borrowedDisplay || !InitializeAddons()) return false;
    if (!initialized) ++fontGeneration;
    display = borrowedDisplay;
    initialized = true;
    SetDisplaySize(al_get_display_width(display), al_get_display_height(display));
    return true;
}
void Allegro5System::Shutdown() {
    SetInputRoot(nullptr);
    for (const FontEntry& entry : fonts) {
        if (entry.font) al_destroy_font(entry.font);
    }
    fonts.clear();
    ++fontGeneration;
    display = nullptr;
    initialized = false;
    // Addons belong to the shared Allegro process, not to this facade.
}
bool Allegro5System::IsInitialized() { return initialized; }
ALLEGRO_DISPLAY* Allegro5System::GetDisplay() { return display; }
int Allegro5System::GetWidth() { return screenWidth; }
int Allegro5System::GetHeight() { return screenHeight; }
void Allegro5System::SetDisplaySize(int width, int height) {
    if (width > 0) screenWidth = width;
    if (height > 0) screenHeight = height;
    scaleX = static_cast<float>(screenWidth) / baseWidth;
    scaleY = static_cast<float>(screenHeight) / baseHeight;
}
ALLEGRO_EVENT_QUEUE* Allegro5System::GetEventQueue() { return nullptr; }
bool Allegro5System::ProcessEvents() { return initialized && display; }
void Allegro5System::SetInputRoot(UIObject* root) {
    if (inputRoot == root) return;
    UIObject* previous = inputRoot;
    inputRoot = nullptr;
    if (previous) previous->ResetInput();
    inputRoot = root;
}
UIObject* Allegro5System::GetInputRoot() { return inputRoot; }
bool Allegro5System::DispatchEvent(const ALLEGRO_EVENT& event) {
    if (event.type==ALLEGRO_EVENT_DISPLAY_RESIZE && event.display.source==display) {
        SetDisplaySize(event.display.width,event.display.height);
        return false; // Gucci acknowledges its own resize event.
    }
    return inputRoot && inputRoot->DispatchEvent(event);
}
void Allegro5System::BeginFrame() {
    if (display) al_set_target_backbuffer(display);
}
void Allegro5System::EndFrame() {
    // Gucci presents once, after both legacy and HD rendering are complete.
}
void Allegro5System::Clear(float r, float g, float b, float a) {
    al_clear_to_color(al_map_rgba_f(r, g, b, a));
}
void Allegro5System::DrawRectFilled(float x, float y, float w, float h,
                                   float r, float g, float b, float a) {
    al_draw_filled_rectangle(ScaleX(x), ScaleY(y), ScaleX(x+w), ScaleY(y+h),
                             al_map_rgba_f(r,g,b,a));
}
void Allegro5System::DrawRectOutline(float x, float y, float w, float h,
                                    float r, float g, float b, float a, float thickness) {
    al_draw_rectangle(ScaleX(x), ScaleY(y), ScaleX(x+w), ScaleY(y+h),
                      al_map_rgba_f(r,g,b,a), thickness * std::min(scaleX, scaleY));
}
void Allegro5System::DrawLine(float x1, float y1, float x2, float y2,
                             float r, float g, float b, float a, float thickness) {
    al_draw_line(ScaleX(x1), ScaleY(y1), ScaleX(x2), ScaleY(y2),
                 al_map_rgba_f(r,g,b,a), thickness * std::min(scaleX,scaleY));
}
void Allegro5System::DrawGradientVertical(float x, float y, float w, float h,
                                         float r1, float g1, float b1, float a1,
                                         float r2, float g2, float b2, float a2) {
    ALLEGRO_VERTEX vertices[4] = {};
    const float px[4] = {x,x+w,x+w,x};
    const float py[4] = {y,y,y+h,y+h};
    for (int i=0; i<4; ++i) {
        vertices[i].x = ScaleX(px[i]);
        vertices[i].y = ScaleY(py[i]);
        vertices[i].color = i < 2 ? al_map_rgba_f(r1,g1,b1,a1) : al_map_rgba_f(r2,g2,b2,a2);
    }
    al_draw_prim(vertices,nullptr,nullptr,0,4,ALLEGRO_PRIM_TRIANGLE_FAN);
}
ALLEGRO_FONT* Allegro5System::LoadFont(const char* name, int size) {
    if (!name || !*name || size <= 0 || !InitializeAddons()) return nullptr;
    for (const FontEntry& entry : fonts) {
        if (entry.size == size && entry.name == name) return entry.font;
    }
    ALLEGRO_FONT* font = al_load_ttf_font(name,size,0);
    if (!font) {
        std::string path=name;
        if (path.size()>=4 && path.compare(path.size()-4,4,".ttf")==0) {
            path.replace(path.size()-4,4,".TTF");
            font=al_load_ttf_font(path.c_str(),size,0);
        }
    }
    fonts.push_back(FontEntry{std::string(name),size,font});
    return font;
}
unsigned Allegro5System::FontGeneration() { return fontGeneration; }
void Allegro5System::DrawText(ALLEGRO_FONT* font, float x, float y, const char* text,
                             float r, float g, float b, float a, int align) {
    if (!font || !text) return;
    ALLEGRO_TRANSFORM previous = *al_get_current_transform();
    ALLEGRO_TRANSFORM transform;
    al_identity_transform(&transform);
    al_scale_transform(&transform,scaleX,scaleY);
    al_compose_transform(&transform,&previous);
    al_use_transform(&transform);
    const int flags = align == 1 ? ALLEGRO_ALIGN_CENTER : align == 2 ? ALLEGRO_ALIGN_RIGHT : 0;
    al_draw_text(font,al_map_rgba_f(r,g,b,a),x,y,flags,text);
    al_use_transform(&previous);
}
ALLEGRO_BITMAP* Allegro5System::LoadBitmap(const char* path) {
    return path && *path && InitializeAddons() ? al_load_bitmap(path) : nullptr;
}
void Allegro5System::DrawBitmap(ALLEGRO_BITMAP* bmp,float x,float y,float alpha) {
    if (bmp) DrawBitmapScaled(bmp,x,y,al_get_bitmap_width(bmp),al_get_bitmap_height(bmp),alpha);
}
void Allegro5System::DrawBitmapScaled(ALLEGRO_BITMAP* bmp,float x,float y,float w,float h,float alpha) {
    if (!bmp) return;
    al_draw_tinted_scaled_bitmap(bmp,al_map_rgba_f(1,1,1,alpha),0,0,
        al_get_bitmap_width(bmp),al_get_bitmap_height(bmp),ScaleX(x),ScaleY(y),ScaleW(w),ScaleH(h),0);
}
void Allegro5System::SetBaseResolution(int width,int height) {
    if (width <= 0 || height <= 0) return;
    baseWidth=width; baseHeight=height;
    SetDisplaySize(screenWidth,screenHeight);
}
float Allegro5System::ScaleX(float x) { return x*scaleX; }
float Allegro5System::ScaleY(float y) { return y*scaleY; }
float Allegro5System::ScaleW(float w) { return w*scaleX; }
float Allegro5System::ScaleH(float h) { return h*scaleY; }
Color Color::FromHex(const char* hex) {
    if (!hex || !*hex) return Color();
    if (*hex == '#') ++hex;
    const size_t length = std::strlen(hex);
    if (length != 3 && length != 6 && length != 8) return Color();
    unsigned value = 0;
    for (size_t i=0; i<length; ++i) {
        const int digit = HexDigit(hex[i]);
        if (digit < 0) return Color();
        value=(value<<4)|static_cast<unsigned>(digit);
    }
    if (length == 3) return Color(((value>>8)&15)/15.0f,((value>>4)&15)/15.0f,(value&15)/15.0f);
    if (length == 8) return Color(((value>>24)&255)/255.0f,((value>>16)&255)/255.0f,
                                  ((value>>8)&255)/255.0f,(value&255)/255.0f);
    return Color(((value>>16)&255)/255.0f,((value>>8)&255)/255.0f,(value&255)/255.0f);
}
Color Color::FromHexWithAlpha(const char* hex,float alpha) {
    Color color=FromHex(hex); color.a*=alpha; return color;
}
} // namespace HDUI
