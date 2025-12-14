/*
 * HD_UI Allegro5 Graphics System Implementation
 */

#include "hd_allegro5.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace HDUI {

// Static member initialization
ALLEGRO_DISPLAY* Allegro5System::display = nullptr;
ALLEGRO_EVENT_QUEUE* Allegro5System::eventQueue = nullptr;
ALLEGRO_TIMER* Allegro5System::timer = nullptr;
int Allegro5System::screenWidth = 1920;
int Allegro5System::screenHeight = 1080;
int Allegro5System::baseWidth = 1920;
int Allegro5System::baseHeight = 1080;
float Allegro5System::scaleX = 1.0f;
float Allegro5System::scaleY = 1.0f;
bool Allegro5System::initialized = false;

bool Allegro5System::Initialize(int width, int height, bool fullscreen) {
    if (initialized) return true;
    
    // Initialize Allegro
    if (!al_init()) {
        fprintf(stderr, "HD_UI: Failed to initialize Allegro5!\n");
        return false;
    }
    
    // Initialize addons
    if (!al_init_primitives_addon()) {
        fprintf(stderr, "HD_UI: Failed to initialize primitives addon!\n");
        return false;
    }
    
    if (!al_init_font_addon()) {
        fprintf(stderr, "HD_UI: Failed to initialize font addon!\n");
        return false;
    }
    
    if (!al_init_ttf_addon()) {
        fprintf(stderr, "HD_UI: Failed to initialize TTF addon!\n");
        return false;
    }
    
    if (!al_init_image_addon()) {
        fprintf(stderr, "HD_UI: Failed to initialize image addon!\n");
        return false;
    }
    
    // Install keyboard and mouse
    if (!al_install_keyboard()) {
        fprintf(stderr, "HD_UI: Failed to install keyboard!\n");
        return false;
    }
    
    if (!al_install_mouse()) {
        fprintf(stderr, "HD_UI: Failed to install mouse!\n");
        return false;
    }
    
    // Set display flags
    int flags = ALLEGRO_OPENGL | ALLEGRO_PROGRAMMABLE_PIPELINE;
    if (fullscreen) {
        flags |= ALLEGRO_FULLSCREEN_WINDOW;
    } else {
        flags |= ALLEGRO_RESIZABLE;
    }
    al_set_new_display_flags(flags);
    
    // Set OpenGL version (use 3.0 for compatibility like the mod)
    al_set_new_display_option(ALLEGRO_OPENGL_MAJOR_VERSION, 3, ALLEGRO_SUGGEST);
    al_set_new_display_option(ALLEGRO_OPENGL_MINOR_VERSION, 0, ALLEGRO_SUGGEST);
    al_set_new_display_option(ALLEGRO_SAMPLE_BUFFERS, 1, ALLEGRO_SUGGEST);
    al_set_new_display_option(ALLEGRO_SAMPLES, 4, ALLEGRO_SUGGEST);
    
    // Create display
    // Use gucci's display instead of creating a new one
    display = al_get_current_display();
    if (!display) {
        fprintf(stderr, "HD_UI: No current display, creating one...\\n");
        display = al_create_display(width, height);
    }
    if (!display) {
        fprintf(stderr, "HD_UI: Failed to create display!\n");
        // Try safe mode without multisampling
        al_set_new_display_option(ALLEGRO_SAMPLE_BUFFERS, 0, ALLEGRO_SUGGEST);
        al_set_new_display_option(ALLEGRO_SAMPLES, 0, ALLEGRO_SUGGEST);
        // Use gucci's display instead of creating a new one
    display = al_get_current_display();
    if (!display) {
        fprintf(stderr, "HD_UI: No current display, creating one...\\n");
        display = al_create_display(width, height);
    }
        if (!display) {
            fprintf(stderr, "HD_UI: Failed to create display even in safe mode!\n");
            return false;
        }
    }
    
    // Get actual display dimensions
    screenWidth = al_get_display_width(display);
    screenHeight = al_get_display_height(display);
    
    // Create timer (60 FPS)
    timer = al_create_timer(1.0 / 60.0);
    if (!timer) {
        fprintf(stderr, "HD_UI: Failed to create timer!\n");
        al_destroy_display(display);
        return false;
    }
    
    // Create event queue
    eventQueue = al_create_event_queue();
    if (!eventQueue) {
        fprintf(stderr, "HD_UI: Failed to create event queue!\n");
        al_destroy_timer(timer);
        al_destroy_display(display);
        return false;
    }
    
    // Register event sources
    al_register_event_source(eventQueue, al_get_display_event_source(display));
    al_register_event_source(eventQueue, al_get_timer_event_source(timer));
    al_register_event_source(eventQueue, al_get_keyboard_event_source());
    al_register_event_source(eventQueue, al_get_mouse_event_source());
    
    // Start timer
    al_start_timer(timer);
    
    // Set window title
    al_set_window_title(display, "Uplink HD");
    
    // Calculate scaling
    scaleX = (float)screenWidth / (float)baseWidth;
    scaleY = (float)screenHeight / (float)baseHeight;
    
    initialized = true;
    printf("HD_UI: Allegro5 initialized at %dx%d\n", width, height);
    
    return true;
}

void Allegro5System::Shutdown() {
    if (!initialized) return;
    
    if (eventQueue) {
        al_destroy_event_queue(eventQueue);
        eventQueue = nullptr;
    }
    
    if (timer) {
        al_destroy_timer(timer);
        timer = nullptr;
    }
    
    if (display) {
        al_destroy_display(display);
        display = nullptr;
    }
    
    al_shutdown_font_addon();
    al_shutdown_ttf_addon();
    al_shutdown_image_addon();
    al_shutdown_primitives_addon();
    
    initialized = false;
    printf("HD_UI: Allegro5 shutdown complete\n");
}

bool Allegro5System::IsInitialized() {
    return initialized;
}

ALLEGRO_DISPLAY* Allegro5System::GetDisplay() {
    return display;
}

int Allegro5System::GetWidth() {
    return screenWidth;
}

int Allegro5System::GetHeight() {
    return screenHeight;
}

void Allegro5System::SetDisplaySize(int width, int height) {
    // Get actual display dimensions
    screenWidth = al_get_display_width(display);
    screenHeight = al_get_display_height(display);
    scaleX = (float)screenWidth / (float)baseWidth;
    scaleY = (float)screenHeight / (float)baseHeight;
}

ALLEGRO_EVENT_QUEUE* Allegro5System::GetEventQueue() {
    return eventQueue;
}

bool Allegro5System::ProcessEvents() {
    ALLEGRO_EVENT event;
    while (al_get_next_event(eventQueue, &event)) {
        if (event.type == ALLEGRO_EVENT_DISPLAY_CLOSE) {
            return false; // Signal to quit
        }
        if (event.type == ALLEGRO_EVENT_DISPLAY_RESIZE) {
            al_acknowledge_resize(display);
            SetDisplaySize(al_get_display_width(display), 
                          al_get_display_height(display));
        }
        // TODO: Handle keyboard/mouse events
    }
    return true;
}

void Allegro5System::BeginFrame() {
    al_set_target_backbuffer(display);
}

void Allegro5System::EndFrame() {
    al_flip_display();
}

void Allegro5System::Clear(float r, float g, float b, float a) {
    al_clear_to_color(al_map_rgba_f(r, g, b, a));
}

void Allegro5System::DrawRectFilled(float x, float y, float w, float h,
                                     float r, float g, float b, float a) {
    float sx = ScaleX(x);
    float sy = ScaleY(y);
    float sw = ScaleW(w);
    float sh = ScaleH(h);
    al_draw_filled_rectangle(sx, sy, sx + sw, sy + sh, al_map_rgba_f(r, g, b, a));
}

void Allegro5System::DrawRectOutline(float x, float y, float w, float h,
                                      float r, float g, float b, float a, float thickness) {
    float sx = ScaleX(x);
    float sy = ScaleY(y);
    float sw = ScaleW(w);
    float sh = ScaleH(h);
    al_draw_rectangle(sx, sy, sx + sw, sy + sh, al_map_rgba_f(r, g, b, a), thickness);
}

void Allegro5System::DrawLine(float x1, float y1, float x2, float y2,
                              float r, float g, float b, float a, float thickness) {
    al_draw_line(ScaleX(x1), ScaleY(y1), ScaleX(x2), ScaleY(y2),
                 al_map_rgba_f(r, g, b, a), thickness);
}

void Allegro5System::DrawGradientVertical(float x, float y, float w, float h,
                                          float r1, float g1, float b1, float a1,
                                          float r2, float g2, float b2, float a2) {
    float sx = ScaleX(x);
    float sy = ScaleY(y);
    float sw = ScaleW(w);
    float sh = ScaleH(h);
    
    ALLEGRO_VERTEX vertices[4];
    
    // Top-left
    vertices[0].x = sx;
    vertices[0].y = sy;
    vertices[0].z = 0;
    vertices[0].color = al_map_rgba_f(r1, g1, b1, a1);
    
    // Top-right  
    vertices[1].x = sx + sw;
    vertices[1].y = sy;
    vertices[1].z = 0;
    vertices[1].color = al_map_rgba_f(r1, g1, b1, a1);
    
    // Bottom-right
    vertices[2].x = sx + sw;
    vertices[2].y = sy + sh;
    vertices[2].z = 0;
    vertices[2].color = al_map_rgba_f(r2, g2, b2, a2);
    
    // Bottom-left
    vertices[3].x = sx;
    vertices[3].y = sy + sh;
    vertices[3].z = 0;
    vertices[3].color = al_map_rgba_f(r2, g2, b2, a2);
    
    al_draw_prim(vertices, NULL, NULL, 0, 4, ALLEGRO_PRIM_TRIANGLE_FAN);
}

ALLEGRO_FONT* Allegro5System::LoadFont(const char* path, int size) {
    ALLEGRO_FONT* font = al_load_ttf_font(path, size, 0);
    if (!font) {
        fprintf(stderr, "HD_UI: Failed to load font: %s\n", path);
    }
    return font;
}

void Allegro5System::DrawText(ALLEGRO_FONT* font, float x, float y,
                              const char* text, float r, float g, float b, float a,
                              int align) {
    if (!font || !text) return;
    
    int flags = 0;
    if (align == 1) flags = ALLEGRO_ALIGN_CENTER;
    else if (align == 2) flags = ALLEGRO_ALIGN_RIGHT;
    
    al_draw_text(font, al_map_rgba_f(r, g, b, a), 
                 ScaleX(x), ScaleY(y), flags, text);
}

ALLEGRO_BITMAP* Allegro5System::LoadBitmap(const char* path) {
    ALLEGRO_BITMAP* bmp = al_load_bitmap(path);
    if (!bmp) {
        fprintf(stderr, "HD_UI: Failed to load bitmap: %s\n", path);
    }
    return bmp;
}

void Allegro5System::DrawBitmap(ALLEGRO_BITMAP* bmp, float x, float y, float alpha) {
    if (!bmp) return;
    al_draw_tinted_bitmap(bmp, al_map_rgba_f(1, 1, 1, alpha), ScaleX(x), ScaleY(y), 0);
}

void Allegro5System::DrawBitmapScaled(ALLEGRO_BITMAP* bmp, float x, float y,
                                       float w, float h, float alpha) {
    if (!bmp) return;
    al_draw_tinted_scaled_bitmap(bmp, al_map_rgba_f(1, 1, 1, alpha),
                                  0, 0, al_get_bitmap_width(bmp), al_get_bitmap_height(bmp),
                                  ScaleX(x), ScaleY(y), ScaleW(w), ScaleH(h), 0);
}

void Allegro5System::SetBaseResolution(int bw, int bh) {
    baseWidth = bw;
    baseHeight = bh;
    scaleX = (float)screenWidth / (float)baseWidth;
    scaleY = (float)screenHeight / (float)baseHeight;
}

float Allegro5System::ScaleX(float x) {
    return x * scaleX;
}

float Allegro5System::ScaleY(float y) {
    return y * scaleY;
}

float Allegro5System::ScaleW(float w) {
    return w * scaleX;
}

float Allegro5System::ScaleH(float h) {
    return h * scaleY;
}

// Color utility implementation
Color Color::FromHex(const char* hex) {
    if (!hex) return Color();
    
    // Skip leading # if present
    if (hex[0] == '#') hex++;
    
    unsigned int r = 0, g = 0, b = 0;
    if (strlen(hex) >= 6) {
        char rr[3] = {hex[0], hex[1], 0};
        char gg[3] = {hex[2], hex[3], 0};  
        char bb[3] = {hex[4], hex[5], 0};
        r = strtoul(rr, NULL, 16);
        g = strtoul(gg, NULL, 16);
        b = strtoul(bb, NULL, 16);
    }
    
    return Color(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
}

Color Color::FromHexWithAlpha(const char* hex, float alpha) {
    Color c = FromHex(hex);
    c.a = alpha;
    return c;
}

} // namespace HDUI
