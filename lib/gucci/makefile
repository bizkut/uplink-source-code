GLUT_OBJECTS = gucci_glut.o resize_unix.o
SDL_OBJECTS = gucci_sdl.o
ALLEGRO_OBJECTS = gucci_allegro5.o

# Use Allegro5 backend for HD UI
OBJECTS = gucci.o image.o $(ALLEGRO_OBJECTS)

libgucci.a : $(OBJECTS)

include ../standard.mk
include ../includes.mk

# Allegro5 flags
CXXFLAGS += -DUSE_ALLEGRO5 -DUSE_FTGL $(shell pkg-config --cflags allegro-5 allegro_main-5)
