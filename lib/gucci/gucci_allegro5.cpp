/*
 * Gucci Allegro5 Backend
 * Replaces gucci_sdl.cpp for HD UI mode
 */

#ifdef USE_ALLEGRO5

#ifdef WIN32
#include <windows.h>
#else
#include <sys/time.h>
#include <sys/resource.h>
#endif

#include <list>
#include <iostream>

#include <allegro5/allegro.h>
#include <allegro5/allegro_opengl.h>
#include <allegro5/allegro_primitives.h>
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_ttf.h>
#include <allegro5/allegro_image.h>
#include <GL/gl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "gucci.h"
#include "gucci_internal.h"

using namespace std;

#include "mmgr.h"

#define GciAbs(num) (((num)>0)?(num):-(num))

// Allegro5 globals
static ALLEGRO_DISPLAY *display = NULL;
static ALLEGRO_EVENT_QUEUE *eventQueue = NULL;
static ALLEGRO_TIMER *timer = NULL;

static unsigned _GciGetAccurateTime();

static bool _GciIsInitGraphicsLibrary = false;

// Key mapping: Allegro5 to Gucci
static int allegroKeyToGucci(int key)
{
  switch (key) {
  case ALLEGRO_KEY_F1: return GCI_KEY_F1;
  case ALLEGRO_KEY_F2: return GCI_KEY_F2;
  case ALLEGRO_KEY_F3: return GCI_KEY_F3;
  case ALLEGRO_KEY_F4: return GCI_KEY_F4;
  case ALLEGRO_KEY_F5: return GCI_KEY_F5;
  case ALLEGRO_KEY_F6: return GCI_KEY_F6;
  case ALLEGRO_KEY_F7: return GCI_KEY_F7;
  case ALLEGRO_KEY_F8: return GCI_KEY_F8;
  case ALLEGRO_KEY_F9: return GCI_KEY_F9;
  case ALLEGRO_KEY_F10: return GCI_KEY_F10;
  case ALLEGRO_KEY_F11: return GCI_KEY_F11;
  case ALLEGRO_KEY_F12: return GCI_KEY_F12;
  default: return 1000 + key;
  }
}

static int allegroButtonToGucci(unsigned int button)
{
  switch (button) {
  case 1: return GCI_LEFT_BUTTON;
  case 2: return GCI_RIGHT_BUTTON;
  case 3: return GCI_MIDDLE_BUTTON;
  default: return button;
  }
}

// Initialize Allegro5 graphics
char *GciInitGraphicsLibrary(int graphics_flags, 
                              int screenWidth, int screenHeight, int screenDepth,
                              int argc, char **argv,
                              char *caption)
{
  bool debugging = false;
  if (argc >= 2 && strcmp(argv[1], "-debug") == 0)
    debugging = true;

  if (debugging) printf("Initialising Allegro5...");

  // Initialize Allegro5
  if (!al_init()) {
    return strdup("Could not initialize Allegro5");
  }

  // Initialize addons
  al_init_primitives_addon();
  al_init_font_addon();
  al_init_ttf_addon();
  al_init_image_addon();
  
  if (!al_install_keyboard()) {
    return strdup("Could not install Allegro5 keyboard");
  }
  
  if (!al_install_mouse()) {
    return strdup("Could not install Allegro5 mouse");
  }

  if (debugging) printf("done\n");

  if (debugging) printf(" Allegro5 is now opening a %dx%d window in %d depth ...", 
                        screenWidth, screenHeight, screenDepth);

  // Set display flags (ALLEGRO_OPENGL for OpenGL rendering)
  int flags = ALLEGRO_OPENGL | ALLEGRO_OPENGL_3_0 | ALLEGRO_WINDOWED;
  if (graphics_flags & GCI_FULLSCREEN) {
    flags = ALLEGRO_OPENGL | ALLEGRO_OPENGL_3_0 | ALLEGRO_FULLSCREEN_WINDOW;
  }
  
  al_set_new_display_flags(flags);
  
  // OpenGL 2.1 for compatibility with existing code
  al_set_new_display_option(ALLEGRO_OPENGL_MAJOR_VERSION, 3, ALLEGRO_SUGGEST);
  al_set_new_display_option(ALLEGRO_OPENGL_MINOR_VERSION, 0, ALLEGRO_SUGGEST);
  
  // Create display
  display = al_create_display(screenWidth, screenHeight);
  if (!display) {
    return strdup("Could not create Allegro5 display");
  }

  if (debugging) printf("done\n");
  if (debugging) printf(" Allegro5 is now changing the window caption and diverse settings ...");

  // Set window title
  al_set_window_title(display, caption);

  // Create event queue
  eventQueue = al_create_event_queue();
  if (!eventQueue) {
    al_destroy_display(display);
    return strdup("Could not create Allegro5 event queue");
  }

  // Create timer (60 FPS)
  timer = al_create_timer(1.0 / 60.0);
  if (!timer) {
    al_destroy_event_queue(eventQueue);
    al_destroy_display(display);
    return strdup("Could not create Allegro5 timer");
  }

  // Register event sources
  al_register_event_source(eventQueue, al_get_display_event_source(display));
  al_register_event_source(eventQueue, al_get_keyboard_event_source());
  al_register_event_source(eventQueue, al_get_mouse_event_source());
  al_register_event_source(eventQueue, al_get_timer_event_source(timer));

  al_start_timer(timer);

  if (debugging) printf("done\n");
  if (debugging) printf(" Initialising OpenGL...\n");

  // OpenGL is already initialized by ALLEGRO_OPENGL flag
  
  if (debugging) printf("Finished initialising OpenGL.\n");
  if (debugging) printf("Now registering callback functions...");

  _GciIsInitGraphicsLibrary = true;

  if (debugging) printf("done\n ");

  return NULL;
}

// Callback function pointers
extern int gci_defaultfont;

#define GUCCI_FUNC(Function) \
static Gci##Function##FuncT *gci##Function##HandlerP = 0; \
void Gci##Function##Func( Gci##Function##FuncT * f) \
{ \
    gci##Function##HandlerP = f; \
}

GUCCI_FUNC(Display);
GUCCI_FUNC(Motion);
GUCCI_FUNC(PassiveMotion);
GUCCI_FUNC(Keyboard);
GUCCI_FUNC(Idle);
GUCCI_FUNC(Reshape);
GUCCI_FUNC(Mouse);
GUCCI_FUNC(Special);

static bool gciRedisplay = true;
static bool displayDamaged = false;
static bool finished = false;

bool GciLayerDamaged()
{
  return displayDamaged;
}

void GciSwapBuffers()
{
  al_flip_display();
  displayDamaged = false;
}

void GciPostRedisplay()
{
  gciRedisplay = displayDamaged = true;
}

// Timer callback list
class Callback {
public:
  Callback(unsigned duration, GciCallbackT *callback, int value)
    : callback(callback), value(value)
  {
    expiryTime = al_get_time() * 1000 + duration;
  };

  bool expired() {
    return (al_get_time() * 1000) > expiryTime;
  };

  void invoke();

private:
  GciCallbackT *callback;
  int value;
  double expiryTime;
};

typedef list<Callback *> TimerList;
static TimerList timerEvents;

static void GciProcessTimerEvents()
{
  for (TimerList::iterator i = timerEvents.begin(); i != timerEvents.end(); ) {
    Callback *c = *i;
    if (c->expired()) {
      i = timerEvents.erase(i);
      c->invoke();
      delete c;
    }
    else
      i++;
  }
}

void Callback::invoke()
{
  (*callback)(value);
};

void GciTimerFunc(unsigned int millis, GciCallbackT *callback, int value)
{
  timerEvents.push_back(new Callback(millis, callback, value));
}

// Main event loop
void GciMainLoop()
{
  finished = false;

  while (!finished) {
    // Set OpenGL context before rendering
    al_set_target_backbuffer(display);
    if (gciDisplayHandlerP)
      (*gciDisplayHandlerP)();

    ALLEGRO_EVENT event;

    while (al_get_next_event(eventQueue, &event) && !finished) {
      switch (event.type) {
      case ALLEGRO_EVENT_KEY_DOWN:
      case ALLEGRO_EVENT_KEY_CHAR: {
        int gciKey = allegroKeyToGucci(event.keyboard.keycode);
        
        if (event.type == ALLEGRO_EVENT_KEY_CHAR && event.keyboard.unichar > 0) {
          // Regular character
          if (gciKeyboardHandlerP)
            (*gciKeyboardHandlerP)(event.keyboard.unichar, 
                                   al_get_mouse_state_axis(NULL, 0),
                                   al_get_mouse_state_axis(NULL, 1));
        } else {
          // Special key
          if (gciSpecialHandlerP)
            (*gciSpecialHandlerP)(gciKey,
                                  al_get_mouse_state_axis(NULL, 0),
                                  al_get_mouse_state_axis(NULL, 1));
        }
        break;
      }

      case ALLEGRO_EVENT_MOUSE_AXES: {
        ALLEGRO_MOUSE_STATE state;
        al_get_mouse_state(&state);
        
        if (state.buttons) {
          if (gciMotionHandlerP)
            (*gciMotionHandlerP)(event.mouse.x, event.mouse.y);
        } else {
          if (gciPassiveMotionHandlerP)
            (*gciPassiveMotionHandlerP)(event.mouse.x, event.mouse.y);
        }
        break;
      }

      case ALLEGRO_EVENT_MOUSE_BUTTON_DOWN: {
        if (gciMouseHandlerP)
          (*gciMouseHandlerP)(allegroButtonToGucci(event.mouse.button),
                              GCI_DOWN,
                              event.mouse.x, event.mouse.y);
        break;
      }

      case ALLEGRO_EVENT_MOUSE_BUTTON_UP: {
        if (gciMouseHandlerP)
          (*gciMouseHandlerP)(allegroButtonToGucci(event.mouse.button),
                              GCI_UP,
                              event.mouse.x, event.mouse.y);
        break;
      }

      case ALLEGRO_EVENT_DISPLAY_CLOSE: {
        finished = true;
        break;
      }

      case ALLEGRO_EVENT_DISPLAY_RESIZE: {
        al_acknowledge_resize(display);
        if (gciReshapeHandlerP)
          (*gciReshapeHandlerP)(al_get_display_width(display),
                                al_get_display_height(display));
        break;
      }

      default:
        break;
      }
    }

    // Process timer events
    GciProcessTimerEvents();

    // Idle callback
    if (gciIdleHandlerP)
      (*gciIdleHandlerP)();
  }
}

void GciFinishLoop()
{
  finished = true;
}

static unsigned _GciGetAccurateTime()
{
  return (unsigned)(al_get_time() * 1000);
}

unsigned GciGetAccurateTime()
{
  return _GciGetAccurateTime();
}

int GciGetWindowWidth()
{
  return display ? al_get_display_width(display) : 0;
}

int GciGetWindowHeight()
{
  return display ? al_get_display_height(display) : 0;
}

void GciRestoreScreenSize()
{
  // Not needed with Allegro5 windowed mode
}

bool GciIsInitGraphicsLibrary()
{
  return _GciIsInitGraphicsLibrary;
}

void GciDeleteGraphicsLibrary()
{
  if (timer) {
    al_destroy_timer(timer);
    timer = NULL;
  }
  if (eventQueue) {
    al_destroy_event_queue(eventQueue);
    eventQueue = NULL;
  }
  if (display) {
    al_destroy_display(display);
    display = NULL;
  }
  
  _GciIsInitGraphicsLibrary = false;
}

#endif // USE_ALLEGRO5

// Additional required functions

char *GciInitGraphicsLibrary_simple(int graphics_flags)
{
  bool debugging = (graphics_flags & GCI_DEBUGSTART) != 0;

  if (debugging) printf("Initialising Allegro5...");

  if (!al_init()) {
    return strdup("Could not initialize Allegro5");
  }

  if (debugging) printf("done\n ");

  _GciIsInitGraphicsLibrary = true;
  return NULL;
}

char *GciInitGraphics(const char *caption, int graphics_flags, int screenWidth, int screenHeight,
                      int screenDepth, int screenRefresh, int argc, char *argv[])
{
  return GciInitGraphicsLibrary(graphics_flags, screenWidth, screenHeight, screenDepth,
                                 argc, argv, (char*)caption);
}

void GciFallbackDrawText(int x, int y, char *text, int STYLE)
{
  // Minimal implementation - real text drawing handled by FTGL
}

int GciFallbackTextWidth(char *text, int STYLE)
{
  return strlen(text) * 8; // Rough estimate
}

GciScreenModeList *GciListScreenModes() 
{
  GciScreenModeList *resolutions = new DArray<GciScreenMode *>;
  
  // Add common resolutions for Allegro5
  int modes[][2] = {
    {1920, 1080}, {1680, 1050}, {1600, 900}, {1440, 900},
    {1366, 768}, {1280, 1024}, {1280, 720}, {1024, 768},
    {800, 600}, {640, 480}
  };
  
  for (int i = 0; i < 10; i++) {
    GciScreenMode *newMode = new GciScreenMode;
    newMode->w = modes[i][0];
    newMode->h = modes[i][1];
    resolutions->PutData(newMode);
  }
  
  return resolutions;
}

GciScreenMode *GciGetClosestScreenMode(int width, int height) 
{
  GciScreenMode *newMode = new GciScreenMode;
  newMode->w = width;
  newMode->h = height;
  return newMode;
}

void GciDeleteScreenModeArrayData(GciScreenModeList *modes) 
{
  if (modes == NULL) return;

  for (int i = 0; i < modes->Size(); ++i)
    if (modes->ValidIndex(i))
      if (modes->GetData(i))
        delete modes->GetData(i);
  modes->Empty();
}

void GciSaveScreenshot(const char *file) 
{
  if (!display) return;
  
  ALLEGRO_BITMAP *screenshot = al_get_backbuffer(display);
  if (screenshot) {
    al_save_bitmap(file, screenshot);
  }
}

bool GciAppVisible()
{
  // Allegro5 doesn't have direct app visibility check
  // Return true as default (window is assumed visible)
  return true;
}


// Proper 1-arg version that sets up Allegro5 core
char *GciInitGraphicsLibrary(int graphics_flags)
{
  bool debugging = (graphics_flags & GCI_DEBUGSTART) != 0;

  if (debugging) printf("Initialising Allegro5...");

  if (!al_init()) {
    return strdup("Could not initialize Allegro5");
  }

  // Initialize all addons
  al_init_primitives_addon();
  al_init_font_addon();
  al_init_ttf_addon();
  al_init_image_addon();
  
  if (!al_install_keyboard()) {
    return strdup("Could not install Allegro5 keyboard");
  }
  
  if (!al_install_mouse()) {
    return strdup("Could not install Allegro5 mouse");
  }

  if (debugging) printf("done\n ");

  _GciIsInitGraphicsLibrary = true;
  return NULL;
}
