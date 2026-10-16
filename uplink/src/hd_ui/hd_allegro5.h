#ifndef _included_hd_allegro5_h
#define _included_hd_allegro5_h

#include <allegro5/allegro.h>
#include <allegro5/allegro_primitives.h>
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_ttf.h>
#include <allegro5/allegro_image.h>

namespace HDUI {
class UIObject;

// Gucci owns the display, input sources, event queue and presentation. This
// facade borrows the display and owns only fonts returned by LoadFont.
class Allegro5System {
public:
    static bool Initialize(int width, int height, bool fullscreen);
    static bool AttachDisplay(ALLEGRO_DISPLAY* borrowedDisplay);
    static void Shutdown();
    static bool IsInitialized();
    static ALLEGRO_DISPLAY* GetDisplay();
    static int GetWidth();
    static int GetHeight();
    static void SetDisplaySize(int width, int height);
    static ALLEGRO_EVENT_QUEUE* GetEventQueue();
    static bool ProcessEvents();
    static void SetInputRoot(UIObject* root);
    static UIObject* GetInputRoot();
    static bool DispatchEvent(const ALLEGRO_EVENT& event);
    static void BeginFrame();
    static void EndFrame();
    static void Clear(float r, float g, float b, float a = 1.0f);
    static void DrawRectFilled(float x, float y, float w, float h,
                               float r, float g, float b, float a);
    static void DrawRectOutline(float x, float y, float w, float h,
                                float r, float g, float b, float a, float thickness);
    static void DrawLine(float x1, float y1, float x2, float y2,
                         float r, float g, float b, float a, float thickness);
    static void DrawGradientVertical(float x, float y, float w, float h,
                                     float r1, float g1, float b1, float a1,
                                     float r2, float g2, float b2, float a2);
    static ALLEGRO_FONT* LoadFont(const char* name, int size);
    static void DrawText(ALLEGRO_FONT* font, float x, float y, const char* text,
                         float r, float g, float b, float a, int align = 0);
    // LoadBitmap transfers ownership to the caller.
    static ALLEGRO_BITMAP* LoadBitmap(const char* path);
    static void DrawBitmap(ALLEGRO_BITMAP* bmp, float x, float y, float alpha = 1.0f);
    static void DrawBitmapScaled(ALLEGRO_BITMAP* bmp, float x, float y,
                                 float w, float h, float alpha = 1.0f);
    static void SetBaseResolution(int baseWidth, int baseHeight);
    static float ScaleX(float x);
    static float ScaleY(float y);
    static float ScaleW(float w);
    static float ScaleH(float h);
    static unsigned FontGeneration();
private:
    static ALLEGRO_DISPLAY* display;
    static UIObject* inputRoot;
    static int screenWidth, screenHeight, baseWidth, baseHeight;
    static float scaleX, scaleY;
    static bool initialized;
};

struct Color {
    float r, g, b, a;
    Color() : r(1), g(1), b(1), a(1) {}
    Color(float r, float g, float b, float a = 1.0f) : r(r), g(g), b(b), a(a) {}
    static Color FromHex(const char* hex);
    static Color FromHexWithAlpha(const char* hex, float alpha);
    ALLEGRO_COLOR ToAllegro() const { return al_map_rgba_f(r, g, b, a); }
};
} // namespace HDUI
#endif
