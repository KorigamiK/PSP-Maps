/*
    showrtf:  An example of using the SDL_rtf library
    Copyright (C) 1997, 1998, 1999, 2000, 2001  Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    Sam Lantinga
    slouken@libsdl.org
*/

/* $Id: showrtf.c,v 1.4 2003/08/18 21:38:04 slouken Exp $ */

/* A simple program to test the RTF rendering of the SDL_rtf library */

#include <stdlib.h>
#include <string.h>

#include "SDL.h"
#include "SDL_ttf.h"
#include "SDL_rtf.h"

#if 0
#define SCREEN_WIDTH    640
#define SCREEN_HEIGHT   480
#else
#define SCREEN_WIDTH    320
#define SCREEN_HEIGHT   240
#endif

static const char *FontList[8];

/* Note, this is only one way of looking up fonts */
static int FontFamilyToIndex(RTF_FontFamily family)
{
    switch(family) {
        case RTF_FontDefault:
            return 0;
        case RTF_FontRoman:
            return 1;
        case RTF_FontSwiss:
            return 2;
        case RTF_FontModern:
            return 3;
        case RTF_FontScript:
            return 4;
        case RTF_FontDecor:
            return 5;
        case RTF_FontTech:
            return 6;
        case RTF_FontBidi:
            return 7;
        default:
            return 0;
    }
}

static Uint16 UTF8_to_UNICODE(const char *utf8, int *advance)
{
    int i = 0;
    Uint16 ch;

    ch = ((const unsigned char *)utf8)[i];
    if ( ch >= 0xF0 ) {
        ch  =  (Uint16)(utf8[i]&0x07) << 18;
        ch |=  (Uint16)(utf8[++i]&0x3F) << 12;
        ch |=  (Uint16)(utf8[++i]&0x3F) << 6;
        ch |=  (Uint16)(utf8[++i]&0x3F);
    } else if ( ch >= 0xE0 ) {
        ch  =  (Uint16)(utf8[i]&0x3F) << 12;
        ch |=  (Uint16)(utf8[++i]&0x3F) << 6;
        ch |=  (Uint16)(utf8[++i]&0x3F);
    } else if ( ch >= 0xC0 ) {
        ch  =  (Uint16)(utf8[i]&0x3F) << 6;
        ch |=  (Uint16)(utf8[++i]&0x3F);
    }
    *advance = (i+1);
    return ch;
}
static void *CreateFont(const char *name, RTF_FontFamily family, int size, int style)
{
    int index;
    TTF_Font *font;

    index = FontFamilyToIndex(family);
    if (!FontList[index])
        index = 0;

    font = TTF_OpenFont(FontList[index], size);
    if (font) {
        int TTF_style = TTF_STYLE_NORMAL;
        if ( style & RTF_FontBold )
            TTF_style |= TTF_STYLE_BOLD;
        if ( style & RTF_FontItalic )
            TTF_style |= TTF_STYLE_ITALIC;
        if ( style & RTF_FontUnderline )
            TTF_style |= TTF_STYLE_UNDERLINE;
        TTF_SetFontStyle(font, style);
    }
    return font;
}

static int GetLineSpacing(void *_font)
{
    TTF_Font *font = (TTF_Font *)_font;
    return TTF_FontLineSkip(font);
}

static int GetCharacterOffsets(void *_font, const char *text, int *byteOffsets, int *pixelOffsets, int maxOffsets)
{
    TTF_Font *font = (TTF_Font *)_font;
    int i = 0;
    int bytes = 0;
    int pixels = 0;
    int advance;
    Uint16 ch;
    while ( *text && i < maxOffsets ) {
        byteOffsets[i] = bytes;
        pixelOffsets[i] = pixels;
        ++i;

        ch = UTF8_to_UNICODE(text, &advance);
        text += advance;
        bytes += advance;
        TTF_GlyphMetrics(font, ch, NULL, NULL, NULL, NULL, &advance);
        pixels += advance;
    }
    if ( i < maxOffsets ) {
        byteOffsets[i] = bytes;
        pixelOffsets[i] = pixels;
    }
    return i;
}

static SDL_Surface *RenderText(void *_font, const char *text, SDL_Color fg)
{
    TTF_Font *font = (TTF_Font *)_font;
    return TTF_RenderUTF8_Blended(font, text, fg);
}

static void FreeFont(void *_font)
{
    TTF_Font *font = (TTF_Font *)_font;
    TTF_CloseFont(font);
}

#if 1
static int LoadRTF(RTF_Context *ctx, const char *file)
{
    if ( RTF_Load(ctx, file) < 0 ) {
        fprintf(stderr, "Couldn't load %s: %s\n", file, RTF_GetError());
        return -1;
    }
    //SDL_WM_SetCaption(RTF_GetTitle(ctx), file);
    return 0;
}

#else
static int MakeRTF(RTF_Context *ctx, const char *name, void *mem, int size)
{
    SDL_RWops *rw = SDL_RWFromMem(mem, size);
    if (( rw == NULL ) || ( RTF_Load_RW(ctx, rw, 1) < 0 )) {
        fprintf(stderr, "Couldn't load %s: %s\n", name, RTF_GetError());
        return -1;
    }
    //SDL_WM_SetCaption(RTF_GetTitle(ctx), name);
    return 0;
}
#endif

int show_rtf(SDL_Surface *screen, char *fontname, char *filename, int offset)
{
    int i;
    int done;
    int height;
    RTF_Context *ctx;
    RTF_FontEngine fontEngine;
    Uint32 white;
    Uint8 *keystate;

    FontList[FontFamilyToIndex(RTF_FontDefault)] = strdup(fontname);

    white = SDL_MapRGB(screen->format, 255, 255, 255);

    /* Create and load the RTF document */
    fontEngine.version = RTF_FONT_ENGINE_VERSION;
    fontEngine.CreateFont = CreateFont;
    fontEngine.GetLineSpacing = GetLineSpacing;
    fontEngine.GetCharacterOffsets = GetCharacterOffsets;
    fontEngine.RenderText = RenderText;
    fontEngine.FreeFont = FreeFont;
    ctx = RTF_CreateContext(&fontEngine);
    if ( ctx == NULL ) {
        fprintf(stderr, "Couldn't create RTF context: %s\n", RTF_GetError());
        return(offset);
    }
    // This loads from a file.  I adde MakeRtf() to load from mem for pspmaps.
    if (0 != LoadRTF(ctx, filename))
      return(offset);

    /* Render the document to the screen */
    done = 0;
    if (offset < 0)
      offset = 0;
    height = RTF_GetHeight(ctx, screen->w);
    if ( offset > (height - screen->h) )
      offset = (height - screen->h);
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_VIDEORESIZE) {
                float ratio = (float)offset / height;
                screen = SDL_SetVideoMode(event.resize.w, event.resize.h, 0, SDL_RESIZABLE);
                height = RTF_GetHeight(ctx, screen->w);
                offset = (int)(ratio * height);
            }
            if (event.type == SDL_KEYDOWN) {
                switch(event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                    case SDLK_RETURN:
                    case SDLK_TAB:
                    case SDLK_BACKSPACE:
                        done = 1;
                        break;
                    case SDLK_LEFT:
                    case SDLK_RIGHT:
                        break;
                    case SDLK_HOME:
                        offset = 0;
                        break;
                    case SDLK_END:
                        offset = (height - screen->h);
                        break;
                    case SDLK_PAGEUP:
                    case SDLK_COMMA:
                        offset -= screen->h;
                        if ( offset < 0 )
                            offset = 0;
                        break;
                    case SDLK_PAGEDOWN:
                    case SDLK_PERIOD:
                    case SDLK_SPACE:
                        offset += screen->h;
                        if ( offset > (height - screen->h) )
                            offset = (height - screen->h);
                        break;
                    default:
                        break;
                }
            }
            if (event.type == SDL_QUIT) {
                done = 1;
            }
        }
        keystate = SDL_GetKeyState(NULL);
        if ( keystate[SDLK_UP] ) {
            offset -= 1;
            if ( offset < 0 )
                offset = 0;
        }
        if ( keystate[SDLK_DOWN] ) {
            offset += 1;
            if ( offset > (height - screen->h) )
                offset = (height - screen->h);
        }
        SDL_FillRect(screen, NULL, white);
        RTF_Render(ctx, screen, NULL, offset);
        SDL_UpdateRect(screen, 0, 0, 0, 0);
    }

    /* Clean up and exit */
    RTF_FreeContext(ctx);
    return(offset);
}

extern char txtbuf[];

void rtf_update()
{
  static char buf[1032];
  char *s, *p, *q;
  int i, j, k;
  unsigned int u;
  FILE *f;
  
  if (NULL == (f=fopen("/tmp/route.rtf","w")))
    return;
  // Set Code Page 1252 for latin1.  Not sure if that or utf8 here...
  fprintf(f,"{\\rtf1\\ansi\\ansicpg1252\\deff0 {\\fonttbl {\\f0 Courier;}}\n");
  fprintf(f,"{\\colortbl;\\red0\\green0\\blue0;\\red255\\green0\\blue0;\\red0\\green0\\blue255;}\n");
  for (s = strtok(txtbuf, "\n"); s; s = strtok(NULL, "\n")){
    //j = strlen(s); if (j >1031) j = 1031;
    q = buf;
    for (k = 0; *s; s+=i) {
      u = UTF8_to_UNICODE(s, &i);
      if (u < 0x80)
	q[k++] = u; // ok for latin1, although not really RTF standard.
      else{
	if (u < 0x100)
	  sprintf(q+k, "\\u%d%c", u, u);  // Maybe try a codepage escape?
	else
	  sprintf(q+k, "\\u%d?", u);
	k += strlen(q+k);
      }
    }
    q[k] = 0;
    
    //
    // Need to change utf8 to \\uNNNN? sequence and \,{.} to \\,\{,\} sequences.
    //if ((*p == '\\') ||(*p == '}') ||(*p == '}'))
    //  *q++ = '\\';
    //
    
    p = buf;
    if (p[0] == '(')
      fprintf(f, "\\cf3\n%s\\line\n\\cf1\n",p);
    else if (p[0] == ' ')
      fprintf(f, "\\cf2\n%s\\line\n\\cf1\n",p);
    else
      fprintf(f, "%s\\line\n",p);
    p[strlen(p)] = '\n'; // Replace nulls added by strtok.
  }
  fprintf(f,"}\n");
  fclose(f);
}

