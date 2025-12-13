# FTGL-2.1.2 Makefile for Uplink
# Builds bundled FTGL library with system FreeType2

include ../../standard.mk

FTGL_SRC = src
FTGL_INCLUDE = include

# Use system freetype2
CXXFLAGS = -O2 -I$(FTGL_INCLUDE) $(shell pkg-config --cflags freetype2) -DFTGL_LIBRARY_STATIC

SOURCES = \
    $(FTGL_SRC)/FTBitmapGlyph.cpp \
    $(FTGL_SRC)/FTCharmap.cpp \
    $(FTGL_SRC)/FTContour.cpp \
    $(FTGL_SRC)/FTExtrdGlyph.cpp \
    $(FTGL_SRC)/FTFace.cpp \
    $(FTGL_SRC)/FTFont.cpp \
    $(FTGL_SRC)/FTGLBitmapFont.cpp \
    $(FTGL_SRC)/FTGLExtrdFont.cpp \
    $(FTGL_SRC)/FTGLOutlineFont.cpp \
    $(FTGL_SRC)/FTGLPixmapFont.cpp \
    $(FTGL_SRC)/FTGLPolygonFont.cpp \
    $(FTGL_SRC)/FTGLTextureFont.cpp \
    $(FTGL_SRC)/FTGlyph.cpp \
    $(FTGL_SRC)/FTGlyphContainer.cpp \
    $(FTGL_SRC)/FTLibrary.cpp \
    $(FTGL_SRC)/FTOutlineGlyph.cpp \
    $(FTGL_SRC)/FTPixmapGlyph.cpp \
    $(FTGL_SRC)/FTPoint.cpp \
    $(FTGL_SRC)/FTPolyGlyph.cpp \
    $(FTGL_SRC)/FTSize.cpp \
    $(FTGL_SRC)/FTTextureGlyph.cpp \
    $(FTGL_SRC)/FTVectoriser.cpp

OBJECTS = $(SOURCES:.cpp=.o)

all: libftgl.a

libftgl.a: $(OBJECTS)
	ar rcs $@ $^
	ranlib $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) libftgl.a
