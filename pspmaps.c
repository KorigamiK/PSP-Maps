/*
 * PSP-Maps
 * A homebrew to browse Google Maps, Virtual Earth and Yahoo! Maps with your PSP!
 *
 * Copyright (C) 2008  Antoine Jacquet <royale@zerezo.com>
 * http://royale.zerezo.com/psp/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_rotozoom.h>
#include <SDL_gfxPrimitives.h>
#include <SDL_ttf.h>
#include <curl/curl.h>

#define VERSION "0.8"
#define WIDTH 480
#define HEIGHT 272
#define BPP 32
#define BUFFER_SIZE 100 * 1024
#define MEMORY_CACHE_SIZE 32
#define DIGITAL_STEP 0.5
#define JOYSTICK_STEP 0.05
#define JOYSTICK_DEAD 10000
#define NUM_FAVORITES 9

#define BLACK SDL_MapRGB(screen->format, 0, 0, 0)
#define WHITE SDL_MapRGB(screen->format, 255, 255, 255)

#ifdef _PSP_FW_VERSION
#define DEBUG(x...) {}
#else
#define DEBUG(x...) printf(x);
#endif

#ifdef _PSP_FW_VERSION
#include <pspkernel.h>
#include <pspsdk.h>
#include <psputility.h>
#include <pspnet_apctl.h>
#define printf pspDebugScreenPrintf
#define MODULE_NAME "PSP-Maps"
PSP_MODULE_INFO(MODULE_NAME, 0, 1, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(20480);
void quit();
#include "netdialog.c"
#define DANZEFF_SDL
#include "pspctrl_emu.c"
#include "danzeff.c"
#endif

#ifdef _WIN32
#define bzero(P, N) memset(P, 0, N)
#define mkdir(D, M) mkdir(D)
#endif

SDL_Surface *screen, *prev, *next;
SDL_Surface *logo, *na, *zoom;
SDL_Joystick *joystick;
TTF_Font *font;
CURL *curl;
char response[BUFFER_SIZE];

/* x, y, z are in Google's format: z = [ -4 .. 16 ], x and y = [ 1 .. 2^(17-z) ] */
int z = 16, s = 0;
float x = 1, y = 1, dx, dy;
int active = 0, fav = 0, balancing = 0, radius = 5;

/* cache in memory, for recent history and smooth moves */
struct
{
	int x, y;
	char z, s;
	SDL_Surface *tile;
} memory[MEMORY_CACHE_SIZE];
int memory_idx = 0;

/* cache on disk, for offline browsing and to limit requests */
struct _disk
{
	int x, y;
	char z, s;
} *disk;
int disk_idx = 0;

/* this is the Google Maps API key used for address search
 * this one was created for a dummy domain
 * if it does not work, put your own Google Maps API key here */
char gkey[100] = "ABQIAAAAAslNJJmKiyq8-oFeyctp9xSFOvRczLyAyj57qAvViVrKq19E6hQUo2EXzTDJCL7m3VQT1DNUPzUWAw";

/* user's configuration */
struct
{
	int cache_size;
	int use_effects;
	int show_info;
	int danzeff;
} config;

/* user's favorite places */
struct
{
	float x, y;
	char z, s, ok;
	char name[50];
} favorite[NUM_FAVORITES];

/* Google Maps and Virtual Earth images type */
enum
{
	GG_MAP,
	GG_SATELLITE,
	GG_HYBRID,
	GG_TERRAIN,
	VE_ROAD,
	VE_AERIAL,
	VE_HYBRID,
	VE_HILL,
	YH_MAP,
	YH_SATELLITE,
	YH_HYBRID,
	NUM_VIEWS
};

/* legend for view types */
char *_view[NUM_VIEWS] = {
	"Google Maps / Map",
	"Google Maps / Satellite",
	"Google Maps / Hybrid",
	"Google Maps / Terrain",
	"Virtual Earth / Road",
	"Virtual Earth / Aerial",
	"Virtual Earth / Hybrid",
	"Virtual Earth / Hill",
	"Yahoo! Maps / Map",
	"Yahoo! Maps / Satellite",
	"Yahoo! Maps / Hybrid"
};

/* PSP buttons list */
enum
{
	PSP_BUTTON_Y,
	PSP_BUTTON_B,
	PSP_BUTTON_A,
	PSP_BUTTON_X,
	PSP_BUTTON_L,
	PSP_BUTTON_R,
	PSP_BUTTON_DOWN,
	PSP_BUTTON_LEFT,
	PSP_BUTTON_UP,
	PSP_BUTTON_RIGHT,
	PSP_BUTTON_SELECT,
	PSP_BUTTON_START,
	PSP_NUM_BUTTONS
};

/* transition effects */
enum
{
	FX_NONE,
	FX_LEFT,
	FX_RIGHT,
	FX_UP,
	FX_DOWN,
	FX_IN,
	FX_OUT,
	FX_FADE,
	FX_NUM
};

/* entries in the menu */
enum
{
	MENU_VIEW,
	MENU_ADDRESS,
	MENU_LOAD,
	MENU_SAVE,
	MENU_DEFAULT,
	MENU_INFO,
	MENU_EFFECT,
	MENU_KEYBOARD,
	MENU_RADIUS,
	MENU_CACHE,
	MENU_EXIT,
	MENU_QUIT,
	MENU_NUM
};

/* quit */
void quit()
{
	FILE *f;
	
	/* save disk cache */
	if ((f = fopen("data/disk.dat", "wb")) != NULL)
	{
		fwrite(&disk_idx, sizeof(disk_idx), 1, f);
		fwrite(disk, sizeof(struct _disk), config.cache_size, f);
		fclose(f);
	}
	
	/* save configuration */
	if ((f = fopen("data/config.dat", "wb")) != NULL)
	{
		fwrite(&config, sizeof(config), 1, f);
		fclose(f);
	}
	
	/* save favorites */
	if ((f = fopen("data/favorite.dat", "wb")) != NULL)
	{
		fwrite(favorite, sizeof(favorite), 1, f);
		fclose(f);
	}
	
	/* quit SDL and curl */
	SDL_FreeSurface(prev);
	SDL_FreeSurface(next);
	SDL_Quit();
	curl_easy_cleanup(curl);
	
	/* boom */
	#ifdef _PSP_FW_VERSION
	sceKernelExitGame();
	#else
	printf("quit!\n");
	exit(0);
	#endif
}

/* returns in buffer "b" the name of the Google Maps tile for location (x,y,z) */
void GGtile(int x, int y, int z, char *b)
{
	int c = 18 - z;
	b[c] = '\0';
	while (z++ < 17)
	{
		c--;
		if (x % 2)
		{
			if (y % 2)
				b[c] = 's';
			else
				b[c] = 'r';
		}
		else
		{
			if (y % 2)
				b[c] = 't';
			else
				b[c] = 'q';
		}
		x/=2;
		y/=2;
	}
	b[0] = 't';
}

/* returns in buffer "b" the name of the Virtual Earth tile for location (x,y,z) */
void VEtile(int x, int y, int z, char *b)
{
	int c = 17 - z;
	b[c] = '\0';
	while (z++ < 17)
	{
		c--;
		if (x % 2)
		{
			if (y % 2)
				b[c] = '3';
			else
				b[c] = '1';
		}
		else
		{
			if (y % 2)
				b[c] = '2';
			else
				b[c] = '0';
		}
		x/=2;
		y/=2;
	}
}

/* save tile in memory cache */
void savememory(int x, int y, int z, int s, SDL_Surface *tile)
{
	DEBUG("savememory(%d, %d, %d, %d)\n", x, y, z, s);
	SDL_FreeSurface(memory[memory_idx].tile);
	memory[memory_idx].x = x;
	memory[memory_idx].y = y;
	memory[memory_idx].z = z;
	memory[memory_idx].s = s;
	memory[memory_idx].tile = tile;
	memory_idx = (memory_idx + 1) % MEMORY_CACHE_SIZE;
}

/* return the disk file name for cache entry
 * maximum of 1000 entries per folder to improve access speed */
void diskname(char *buf, int n)
{
	/* create folders if needed */
	sprintf(buf, "cache/%.3d", n/1000);
	mkdir(buf, 0755);
	/* return the full file name */
	sprintf(buf, "cache/%.3d/%.3d.dat", n/1000, n%1000);
}

/* save tile in disk cache */
void savedisk(int x, int y, int z, int s, SDL_RWops *rw, int n)
{
	FILE *f;
	char name[50];
	char buffer[BUFFER_SIZE];
	
	if (!config.cache_size) return;
	
	DEBUG("savedisk(%d, %d, %d, %d)\n", x, y, z, s);
	
	if (rw == NULL)
	{
		printf("warning: savedisk(NULL)!\n");
		return;
	}
	
	disk[disk_idx].x = x;
	disk[disk_idx].y = y;
	disk[disk_idx].z = z;
	disk[disk_idx].s = s;
	
	SDL_RWseek(rw, 0, SEEK_SET);
	diskname(name, disk_idx);
	if ((f = fopen(name, "wb")) != NULL)
	{
		SDL_RWread(rw, buffer, 1, n);
		fwrite(buffer, 1, n, f);
		fclose(f);
	}
	
	disk_idx = (disk_idx + 1) % config.cache_size;
}

/* curl callback to save in memory */
size_t curl_write(void *ptr, size_t size, size_t nb, SDL_RWops *rw)
{
	int t = nb * size;
	rw->write(rw, ptr, size, nb);
	return t;
}

/* get the image on internet and return a buffer */
SDL_RWops *getnet(int x, int y, int z, int s)
{
	char request[1024];
	SDL_RWops *rw;
	
	DEBUG("getnet(%d, %d, %d, %d)\n", x, y, z, s);
	
	switch (s)
	{
		case GG_MAP:
			sprintf(request, "http://mt%d.google.com/mt?n=404&v=w2.69&x=%d&y=%d&zoom=%d", ++balancing%4, x, y, z);
			break;
		case GG_SATELLITE:
			sprintf(request, "http://kh%d.google.com/kh?n=404&v=25&t=", ++balancing%4);
			GGtile(x, y, z, request + strlen(request));
			break;
		case GG_HYBRID:
			sprintf(request, "http://mt%d.google.com/mt?n=404&v=w2t.69&x=%d&y=%d&zoom=%d", ++balancing%4, x, y, z);
			break;
		case GG_TERRAIN:
			sprintf(request, "http://mt%d.google.com/mt?n=404&v=w2p.64&x=%d&y=%d&zoom=%d", ++balancing%4, x, y, z);
			break;
		case VE_ROAD:
			sprintf(request, "http://tiles.virtualearth.net/tiles/r");
			VEtile(x, y, z, request + strlen(request));
			strcat(request, "?g=117");
			break;
		case VE_AERIAL:
			sprintf(request, "http://tiles.virtualearth.net/tiles/a");
			VEtile(x, y, z, request + strlen(request));
			strcat(request, "?g=117");
			break;
		case VE_HYBRID:
			sprintf(request, "http://tiles.virtualearth.net/tiles/h");
			VEtile(x, y, z, request + strlen(request));
			strcat(request, "?g=117");
			break;
		case VE_HILL:
			sprintf(request, "http://tiles.virtualearth.net/tiles/r");
			VEtile(x, y, z, request + strlen(request));
			strcat(request, "?g=117&shading=hill");
			break;
		case YH_MAP:
			sprintf(request, "http://us.maps1.yimg.com/us.tile.yimg.com/tl?v=4.1&x=%d&y=%d&z=%d", x, (int) pow(2, 16-z)-y-1, z+1);
			break;
		case YH_SATELLITE:
			sprintf(request, "http://us.maps3.yimg.com/aerial.maps.yimg.com/ximg?v=1.7&t=a&x=%d&y=%d&z=%d", x, (int) pow(2, 16-z)-y-1, z+1);
			break;
		case YH_HYBRID:
			sprintf(request, "http://us.maps3.yimg.com/aerial.maps.yimg.com/ximg?v=2.5&t=p&x=%d&y=%d&z=%d", x, (int) pow(2, 16-z)-y-1, z+1);
			break;
	}
	
	rw = SDL_RWFromMem(response, BUFFER_SIZE);
	
	//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
	curl_easy_setopt(curl, CURLOPT_URL, request);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, rw);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);
	curl_easy_perform(curl);
	
	return rw;
}

/* return the tile from disk if available, or NULL */
SDL_Surface *getdisk(int x, int y, int z, int s)
{
	int i;
	char name[50];
	DEBUG("getdisk(%d, %d, %d, %d)\n", x, y, z, s);
	for (i = 0; i < config.cache_size; i++)
		if (disk[i].x == x && disk[i].y == y && disk[i].z == z && disk[i].s == s)
		{
			diskname(name, i);
			return IMG_Load(name);
		}
	return NULL;
}

/* return the tile from memory if available, or NULL */
SDL_Surface *getmemory(int x, int y, int z, int s)
{
	int i;
	DEBUG("getmemory(%d, %d, %d, %d)\n", x, y, z, s);
	for (i = 0; i < MEMORY_CACHE_SIZE; i++)
		if (memory[i].tile && memory[i].x == x && memory[i].y == y && memory[i].z == z && memory[i].s == s)
			return memory[i].tile;
	return NULL;
}

/* downloads the image from Google for location (x,y,z) with mode (s) */
SDL_Surface* gettile(int x, int y, int z, int s)
{
	SDL_RWops *rw;
	SDL_Surface *tile;
	int n;
	
	/* try memory cache */
	if ((tile = getmemory(x, y, z, s)) != NULL)
		return tile;
	
	/* try disk cache */
	if ((tile = getdisk(x, y, z, s)) != NULL)
	{
		if (tile == NULL)
			tile = zoomSurface(na, 1, 1, 0);
		savememory(x, y, z, s, tile);
		return tile;
	}
	
	/* try internet */
	rw = getnet(x, y, z, s);
	
	/* load the image */
	n = SDL_RWtell(rw);
	SDL_RWseek(rw, 0, SEEK_SET);
	tile = IMG_Load_RW(rw, 0);
	SDL_RWseek(rw, 0, SEEK_SET);
	
	/* if there is no tile, copy the n/a image
	 * I use a dummy call to zoomSurface to copy the surface
	 * because I had issues with SDL_DisplayFormat() on PSP */
	if (tile == NULL)
		tile = zoomSurface(na, 1, 1, 0);
	/* only save on disk if not n/a
	 * to avoid filling the cache with wrong images
	 * when we are offline */
	else
		savedisk(x, y, z, s, rw, n);
	savememory(x, y, z, s, tile);
	
	SDL_RWclose(rw);
	
	return tile;
}

/* prints a message using the bitmap font */
void print(SDL_Surface *dst, int x, int y, char *text)
{
	SDL_Rect pos;
	SDL_Surface *src;
	SDL_Color color = {255, 255, 255};
	if (font == NULL) return;
	pos.x = x;
	pos.y = y;
	src = TTF_RenderText_Blended(font, text, color);
	SDL_BlitSurface(src, NULL, dst, &pos);
	SDL_FreeSurface(src);
}

/* input text */
void input(SDL_Surface *dst, int x, int y, char *text, int max)
{
	SDL_Event event;
	int action, active = 0, flip;
	int up = 0, down = 0;
	
	void update()
	{
		SDL_Rect pos;
		int xx, yy;
		char tmp;
		
		TTF_SizeText(font, text, &xx, &yy);
		pos.x = x;
		pos.y = y;
		pos.w = WIDTH - x*2;
		pos.h = yy;
		SDL_FillRect(dst, &pos, BLACK);
		
		print(dst, x, y, text);
		/* display blinking cursor */
		if (flip / 5 % 2)
		{
			tmp = text[active];
			text[active] = '\0';
			TTF_SizeText(font, text, &xx, &yy);
			print(dst, x+xx, y, "_");
			text[active] = tmp;
		}
		flip++;
		SDL_BlitSurface(dst, NULL, screen, NULL);
		SDL_Flip(screen);
	}
	
	#ifdef _PSP_FW_VERSION
	/* danzeff */
	if (config.danzeff)
	{
		strcpy(text, "");
		danzeff_load();
		danzeff_set_screen(dst);
		danzeff_moveTo(165, 110);
		
		for (;;)
		{
			SceCtrlData ctrl = getCtrlFromJoystick(joystick);
			int c = danzeff_readInput(ctrl);
			
			switch (c)
			{
				case 0:
					break;
				case '\10':
				case DANZEFF_LEFT:
					if (active > 0)
					{
						active--;
						text[active] = '\0';
					}
					break;
				case DANZEFF_RIGHT:
					if (active < max)
					{
						text[active] = ' ';
						active++;
						text[active] = '\0';
					}
					break;
				case DANZEFF_SELECT:
				case DANZEFF_START:
					return;
				default:
					if (active < max)
					{
						text[active] = c;
						active++;
						text[active] = '\0';
					}
					break;
			}
			
			danzeff_render();
			update();
			SDL_Delay(50);
			
			/* flush events */
			while (SDL_PollEvent(&event));
		}
	}
	else	
	#endif
	
	/* arcade */
	{
		strcpy(text, " ");
		for (;;)
		{
			while (SDL_PollEvent(&event))
			{
				switch (event.type)
				{
					case SDL_QUIT:
						quit();
						break;
					case SDL_KEYDOWN:
					case SDL_JOYBUTTONDOWN:
						if (event.type == SDL_KEYDOWN)
							action = event.key.keysym.sym;
						else
							action = event.jbutton.button;
						switch (action)
						{
							case SDLK_ESCAPE:
							case SDLK_RETURN:
							case SDLK_SPACE:
							case PSP_BUTTON_START:
								return;
							case SDLK_LEFT:
							case PSP_BUTTON_LEFT:
							case PSP_BUTTON_L:
							case PSP_BUTTON_B:
								if (active > 0)
								{
									text[active] = '\0';
									active--;
								}
								break;
							case SDLK_RIGHT:
							case PSP_BUTTON_RIGHT:
							case PSP_BUTTON_R:
							case PSP_BUTTON_A:
							case PSP_BUTTON_X:
							case PSP_BUTTON_Y:
								if (active < max)
								{
									active++;
									text[active] = ' ';
									text[active+1] = '\0';
								}
								break;
							case SDLK_UP:
							case PSP_BUTTON_UP:
								up = 1;
								break;
							case SDLK_DOWN:
							case PSP_BUTTON_DOWN:
								down = 1;
								break;
							default:
								break;
						}
						break;
					case SDL_KEYUP:
					case SDL_JOYBUTTONUP:
						if (event.type == SDL_KEYUP)
							action = event.key.keysym.sym;
						else
							action = event.jbutton.button;
						switch (action)
						{
							case SDLK_UP:
							case PSP_BUTTON_UP:
								up = 0;
								break;
							case SDLK_DOWN:
							case PSP_BUTTON_DOWN:
								down = 0;
								break;
						}
						break;
				}
			}
			if (up == 1 || up > 10)
			{
				if ((text[active] >= 'A' && text[active] < 'Z') || (text[active] >= '0' && text[active] < '9')) text[active]++;
				else if (text[active] == 'Z') text[active] = '0';
				else if (text[active] == '9') text[active] = ' ';
				else if (text[active] == ' ') text[active] = 'A';
			}
			if (up) up++;
			if (down == 1 || down > 10)
			{
				if ((text[active] > 'A' && text[active] <= 'Z') || (text[active] > '0' && text[active] <= '9')) text[active]--;
				else if (text[active] == 'A') text[active] = ' ';
				else if (text[active] == ' ') text[active] = '9';
				else if (text[active] == '0') text[active] = 'Z';
			}
			if (down) down++;
			update();
			SDL_Delay(50);
		}
	}
}

/* displays a box centered at a specific position */
void box(SDL_Surface *dst, int x, int y, int w, int h, int sh)
{
	rectangleRGBA(dst, x - w/2 - 1, y - h/2 - 1, x + w/2 + 1, y + h/2 + 1, 255, 255, 255, 255);
 	boxRGBA(dst, x - w/2, y - h/2, x + w/2, y + h/2, 0, 0, 0, sh);
}

/* fx for transition between prev and next screen */
void effect(int fx)
{
	SDL_Surface *tmp;
	SDL_Rect r;
	int i;
	float t;
	
	if (!config.use_effects) return;
	
	/* effects */
	switch (fx)
	{
		case FX_IN:
			t = 1.0;
			while (t < 2)
			{
				tmp = zoomSurface(prev, t, t, 0);
				r.x = -WIDTH/2 * (t-1);
				r.y = -HEIGHT/2 * (t-1);
				SDL_BlitSurface(tmp, NULL, screen, &r);
				SDL_FreeSurface(tmp);
				SDL_Flip(screen);
				t += 0.1;
				#ifndef _PSP_FW_VERSION
				SDL_Delay(20);
				#endif
			}
			break;
		case FX_OUT:
			t = 2.0;
			while (t > 1)
			{
				tmp = zoomSurface(next, t, t, 0);
				r.x = -WIDTH/2 * (t-1);
				r.y = -HEIGHT/2 * (t-1);
				SDL_BlitSurface(tmp, NULL, screen, &r);
				SDL_FreeSurface(tmp);
				SDL_Flip(screen);
				t -= 0.1;
				#ifndef _PSP_FW_VERSION
				SDL_Delay(20);
				#endif
			}
			break;
		case FX_FADE:
			for (i = 0; i < 255; i+=10)
			{
				tmp = zoomSurface(next, 1, 1, 0);
				SDL_SetAlpha(tmp, SDL_SRCALPHA, i);
				SDL_BlitSurface(prev, NULL, screen, NULL);
				SDL_BlitSurface(tmp, NULL, screen, NULL);
				SDL_FreeSurface(tmp);
				SDL_Flip(screen);
				#ifndef _PSP_FW_VERSION
				SDL_Delay(20);
				#endif
			}
			break;
		case FX_LEFT:
			r.y = 0;
			for (i = 0; i < DIGITAL_STEP * 256; i+=5)
			{
				r.x = i;
				SDL_BlitSurface(prev, NULL, screen, &r);
				r.x = DIGITAL_STEP * 256 - i;
				SDL_BlitSurface(next, &r, screen, NULL);
				SDL_Flip(screen);
				#ifndef _PSP_FW_VERSION
				SDL_Delay(20);
				#endif
			}
			break;
		case FX_RIGHT:
			r.y = 0;
			for (i = 0; i < DIGITAL_STEP * 256; i+=5)
			{
				r.x = i;
				SDL_BlitSurface(prev, &r, screen, NULL);
				r.x = DIGITAL_STEP * 256 - i;
				SDL_BlitSurface(next, NULL, screen, &r);
				SDL_Flip(screen);
				#ifndef _PSP_FW_VERSION
				SDL_Delay(20);
				#endif
			}
			break;
		case FX_UP:
			r.x = 0;
			for (i = 0; i < DIGITAL_STEP * 256; i+=5)
			{
				r.y = i;
				SDL_BlitSurface(prev, NULL, screen, &r);
				r.y = DIGITAL_STEP * 256 - i;
				SDL_BlitSurface(next, &r, screen, NULL);
				SDL_Flip(screen);
				#ifndef _PSP_FW_VERSION
				SDL_Delay(20);
				#endif
			}
			break;
		case FX_DOWN:
			r.x = 0;
			for (i = 0; i < DIGITAL_STEP * 256; i+=5)
			{
				r.y = i;
				SDL_BlitSurface(prev, &r, screen, NULL);
				r.y = DIGITAL_STEP * 256 - i;
				SDL_BlitSurface(next, NULL, screen, &r);
				SDL_Flip(screen);
				#ifndef _PSP_FW_VERSION
				SDL_Delay(20);
				#endif
			}
			break;
	}
}

/* show informations */
void info()
{
	SDL_Rect r;
	char temp[100];
	float lat, lon;
	
	/* show zoomer */
	r.x = WIDTH/2 - 120;
	r.y = HEIGHT/2 - 68;
	SDL_BlitSurface(zoom, NULL, screen, &r);
	
	/* display useful info */
	lon = x / pow(2, 17-z) * 360 - 180;
	lat = y / pow(2, 17-z) * 2 * M_PI;
	lat = atan(exp(M_PI - lat)) / M_PI * 360 - 90;
	hlineRGBA(screen, 0, WIDTH, 16, 255, 255, 255, 255);
	boxRGBA(screen, 0, 0, WIDTH, 15, 0, 0, 0, 200);
	sprintf(temp, "Lat: %10.6f | Lon: %10.6f | Zoom: %3.1d%% | Type: %s", lat, lon, 100*(16-z)/20, _view[s]);
	print(screen, 5, 0, temp);
}

/* updates the display */
void display(int fx)
{
	SDL_Surface *tile;
	SDL_Rect r;
	int i, j, ok;

	/* fix the bounds
	 * disable the special effect to avoid map jumps */
	if (x < 1) { x = 1; fx = FX_NONE; }
	if (x > pow(2, 17-z)-1) { x = pow(2, 17-z)-1; fx = FX_NONE; }
	if (y < 1) { y = 1; fx = FX_NONE; }
	if (y > pow(2, 17-z)-1) { y = pow(2, 17-z)-1; fx = FX_NONE; }
	
	/* save the old screen */
	SDL_BlitSurface(next, NULL, prev, NULL);
	
	/* check if everything is in memory cache */
	ok = 1;
	for (j = y-1; j < y+1; j++)
		for (i = x-1; i < x+1; i++)
			if (!getmemory(i, j, z, s))
				ok = 0;
	
	/* if not, display loading notice */
	if (!ok)
	{
		int x, y;
		SDL_BlitSurface(prev, NULL, screen, NULL);
		box(screen, WIDTH/2, HEIGHT/2, 200, 70, 200);
		TTF_SizeText(font, "LOADING...", &x, &y);
		print(screen, WIDTH/2 - x/2, HEIGHT/2 - 10 - y/2, "LOADING...");
		TTF_SizeText(font, _view[s], &x, &y);
		print(screen, WIDTH/2 - x/2, HEIGHT/2 + 10 - y/2, _view[s]);
		SDL_Flip(screen);
		
	}
	
	/* build the new screen */
	for (j = y-1; j < y+1; j++)
		for (i = x-1; i < x+1; i++)
		{
			/* special process for hybrid maps: compose 2 images */
			r.x = WIDTH/2 + (i-x)*256;
			r.y = HEIGHT/2 + (j-y)*256;
			switch (s)
			{
				case GG_HYBRID:
					tile = gettile(i, j, z, GG_SATELLITE);
					SDL_BlitSurface(tile, NULL, next, &r);
					break;
				case YH_HYBRID:
					tile = gettile(i, j, z, YH_SATELLITE);
					SDL_BlitSurface(tile, NULL, next, &r);
					break;
			}
			
			/* normal process */
			r.x = WIDTH/2 + (i-x)*256;
			r.y = HEIGHT/2 + (j-y)*256;
			tile = gettile(i, j, z, s);
			SDL_BlitSurface(tile, NULL, next, &r);
		}
	
	/* nicer transition */
	effect(fx);
	
	/* restore the good screen */
	SDL_BlitSurface(next, NULL, screen, NULL);
	
	/* show informations */
	if (config.show_info) info();
	
	SDL_Flip(screen);
}

/* lookup address */
void go()
{
	char request[1024], address[50];
	SDL_RWops *rw;
	int i, ret, code, precision;
	float lat, lon;
	char _zoom[9] = {
		16,	// unknown
		12,	// country
		10,	// region 
		9,	// subregion
		7,	// town
		5,	// postcode
		3,	// street
		2,	// intersection
		1,	// exact
	};
	
	box(next, WIDTH/2, HEIGHT/2 - 60, 400, 80, 200);
	print(next, 50, HEIGHT/2 - 90, "Enter address, up/down to change letters, start to validate: ");
	input(next, 50, HEIGHT/2 - 60, address, 46);
	
	DEBUG("go(%s)\n", address);
	
	for (i = 0; i < strlen(address); i++) if (address[i] == ' ') address[i] = '+';
	sprintf(request, "http://maps.google.com/maps/geo?output=csv&key=%s&q=%s", gkey, address);
	
	rw = SDL_RWFromMem(response, BUFFER_SIZE);
	
	//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
	curl_easy_setopt(curl, CURLOPT_URL, request);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, rw);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);
	curl_easy_perform(curl);
	
	ret = sscanf(response, "%d,%d,%f,%f", &code, &precision, &lat, &lon);
	
	if (ret == 4 && code == 200)
	{
		DEBUG("precision: %d, lat: %f, lon: %f\n", precision, lat, lon);
		float e = sin(lat * M_PI / 180);
  		if (e > 0.9999) e = 0.9999;
  		if (e < -0.9999) e = -0.9999;
		z = _zoom[precision];
		x = pow(2, 17-z) * (lon + 180) / 360;
		y = pow(2, 16-z) * (1 - log((1 + e)/(1 - e)) / 2 / M_PI);
	}
	
	SDL_RWclose(rw);
}

/* menu to load/save favorites */
void menu()
{
	SDL_Event event;
	int action, cache_size = config.cache_size;
	int i, j;
	
	#define MENU_LEFT 140
	#define MENU_TOP 65
	#define MENU_BOTTOM 30
	#define MENU_Y (HEIGHT - MENU_TOP - MENU_BOTTOM) / MENU_NUM
	#define MAX_RADIUS 25
	#define ENTRY(position, format...) sprintf(temp, format); print(next, MENU_LEFT, MENU_TOP + position * MENU_Y, temp);
	
	void update()
	{
		char temp[50];
		SDL_Rect pos;
		SDL_FillRect(next, NULL, BLACK);
		pos.x = 80;
		pos.y = 0;
		SDL_BlitSurface(logo, NULL, next, &pos);
		print(next, 280, 30, "version " VERSION);
		print(next, MENU_LEFT-20, MENU_TOP + active * MENU_Y, ">");
		ENTRY(MENU_VIEW, "Current view: %s", _view[s]);
		ENTRY(MENU_ADDRESS, "Enter address...");
		ENTRY(MENU_LOAD, "Load favorite: %d", fav+1);
		ENTRY(MENU_SAVE, "Save favorite: %d", fav+1);
		ENTRY(MENU_DEFAULT, "Default view");
		ENTRY(MENU_INFO, "Show informations: %s", config.show_info ? "Yes" : "No");
		ENTRY(MENU_EFFECT, "Transition effects: %s", config.use_effects ? "Yes" : "No");
		ENTRY(MENU_KEYBOARD, "Keyboard type: %s", config.danzeff ? "Danzeff" : "Arcade");
		ENTRY(MENU_RADIUS, "Cache neighborhood radius: %d", radius);
		ENTRY(MENU_CACHE, "Cache size: %d (~ %d MB)", cache_size, cache_size * 20 / 1000);
		ENTRY(MENU_EXIT, "Exit menu");
		ENTRY(MENU_QUIT, "Quit PSP-Maps");
		print(next, 120, 250, "http://royale.zerezo.com/psp/");
		SDL_BlitSurface(next, NULL, screen, NULL);
		SDL_Flip(screen);
	}
	
	update();
	for (;;)
	{
		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
				case SDL_QUIT:
					quit();
					break;
				case SDL_KEYDOWN:
				case SDL_JOYBUTTONDOWN:
					if (event.type == SDL_KEYDOWN)
						action = event.key.keysym.sym;
					else
						action = event.jbutton.button;
					switch (action)
					{
						case SDLK_ESCAPE:
						case PSP_BUTTON_START:
							return;
						case SDLK_RETURN:
						case SDLK_SPACE:
						case PSP_BUTTON_A:
						case PSP_BUTTON_B:
						case PSP_BUTTON_X:
						case PSP_BUTTON_Y:
							switch (active)
							{
								/* enter address */
								case MENU_ADDRESS:
									go();
									return;
								/* load favorite */
								case MENU_LOAD:
									if (favorite[fav].ok)
									{
										x = favorite[fav].x;
										y = favorite[fav].y;
										z = favorite[fav].z;
										s = favorite[fav].s;
									}
									return;
								/* save favorite */
								case MENU_SAVE:
									favorite[fav].ok = 1;
									favorite[fav].x = x;
									favorite[fav].y = y;
									favorite[fav].z = z;
									favorite[fav].s = s;
									return;
								/* default view */
								case MENU_DEFAULT:
									x = 1;
									y = 1;
									z = 16;
									s = 0;
									return;
								/* infos */
								case MENU_INFO:
									config.show_info = !config.show_info;
									break;
								/* effects */
								case MENU_EFFECT:
									config.use_effects = !config.use_effects;
									break;
								/* keyboard */
								case MENU_KEYBOARD:
									config.danzeff = !config.danzeff;
									break;
								/* radius */
								case MENU_RADIUS:
									box(next, WIDTH/2, HEIGHT/2, 400, 70, 200);
									print(next, 50, HEIGHT/2 - 30, "Loading neighborhood to cache...");
									for (i = 0; i <= 2*radius; i++)
									for (j = 0; j <= 2*radius; j++)
									{
										float ratio = 1.0 * (i*(2*radius+1)+j+1)/(2*radius+1)/(2*radius+1);
										boxRGBA(next, WIDTH/2 - 180, HEIGHT/2, WIDTH/2 - 180 + 360.0 * ratio, HEIGHT/2 + 15, 255, 0, 0, 255);
										/* special process for hybrid maps: get 2 images */
										switch (s)
										{
											case GG_HYBRID:
												gettile(x-radius+i, y-radius+j, z, GG_SATELLITE);
												break;
											case YH_HYBRID:
												gettile(x-radius+i, y-radius+j, z, YH_SATELLITE);
												break;
										}
										gettile(x-radius+i, y-radius+j, z, s);
										SDL_BlitSurface(next, NULL, screen, NULL);
										SDL_Flip(screen);
									}
									break;
								/* disk cache */
								case MENU_CACHE:
									if (config.cache_size != cache_size)
									{
										int old;
										old = config.cache_size;
										config.cache_size = cache_size;
										/* remove data on disk if needed */
										box(next, WIDTH/2, HEIGHT/2, 400, 70, 200);
										print(next, 50, HEIGHT/2 - 30, "Cleaning cache...");
										for (i = config.cache_size; i < disk_idx; i++)
										{
											char name[50];
											float ratio = 1.0 * (i - config.cache_size) / (disk_idx - config.cache_size);
											boxRGBA(next, WIDTH/2 - 180, HEIGHT/2, WIDTH/2 - 180 + 360.0 * ratio, HEIGHT/2 + 15, 255, 0, 0, 255);
											diskname(name, i);
											unlink(name);
											SDL_BlitSurface(next, NULL, screen, NULL);
											SDL_Flip(screen);
										}
										disk = realloc(disk, sizeof(struct _disk) * config.cache_size);
										/* clear newly allocated memory if needed */
										if (config.cache_size > old)
											bzero(&disk[old], sizeof(struct _disk) * (config.cache_size - old));
									}
									break;
								/* exit menu */
								case MENU_VIEW:
								/* view */
								case MENU_EXIT:
									return;
								/* quit PSP-Maps */
								case MENU_QUIT:
									quit();
							}
							update();
							break;
						case SDLK_LEFT:
						case PSP_BUTTON_LEFT:
						case PSP_BUTTON_L:
							switch (active)
							{
								/* view */
								case MENU_VIEW:
									s--;
									if (s < 0) s = NUM_VIEWS-1;
									break;
								/* favorites */
								case MENU_LOAD:
								case MENU_SAVE:
									fav--;
									if (fav < 0) fav = NUM_FAVORITES-1;
									break;
								/* infos */
								case MENU_INFO:
									config.show_info = !config.show_info;
									break;
								/* effects */
								case MENU_EFFECT:
									config.use_effects = !config.use_effects;
									break;
								/* keyboard */
								case MENU_KEYBOARD:
									config.danzeff = !config.danzeff;
									break;
								/* radius */
								case MENU_RADIUS:
									radius--;
									if (radius < 1) radius = MAX_RADIUS;
									break;
								/* disk cache */
								case MENU_CACHE:
									cache_size /= 2;
									if (cache_size == 0) cache_size = 409600;
									if (cache_size < 100) cache_size = 0;
									break;
							}
							update();
							break;
						case SDLK_RIGHT:
						case PSP_BUTTON_RIGHT:
						case PSP_BUTTON_R:
							switch (active)
							{
								/* view */
								case MENU_VIEW:
									s++;
									if (s > NUM_VIEWS-1) s = 0;
									break;
								/* favorites */
								case MENU_LOAD:
								case MENU_SAVE:
									fav++;
									if (fav > NUM_FAVORITES-1) fav = 0;
									break;
								/* infos */
								case MENU_INFO:
									config.show_info = !config.show_info;
									break;
								/* effects */
								case MENU_EFFECT:
									config.use_effects = !config.use_effects;
									break;
								/* keyboard */
								case MENU_KEYBOARD:
									config.danzeff = !config.danzeff;
									break;
								/* radius */
								case MENU_RADIUS:
									radius++;
									if (radius > MAX_RADIUS) radius = 1;
									break;
								/* disk cache */
								case MENU_CACHE:
									cache_size *= 2;
									if (cache_size == 0) cache_size = 100;
									if (cache_size > 409600) cache_size = 0;
									break;
							}
							update();
							break;
						case SDLK_UP:
						case PSP_BUTTON_UP:
							active--;
							if (active < 0) active = MENU_NUM-1;
							update();
							break;
						case SDLK_DOWN:
						case PSP_BUTTON_DOWN:
							active++;
							if (active > MENU_NUM-1) active = 0;
							update();
							break;
						default:
							break;
					}
					break;
			}
		}
		SDL_Delay(10);
	}
}

/* init */
void init()
{
	int flags;
	FILE *f;
	
	/* clear memory cache */
	bzero(memory, sizeof(memory));
	
	/* default options */
	config.cache_size = 1600;
	config.use_effects = 1;
	config.show_info = 0;
	config.danzeff = 1;
	
	/* load configuration if available */
	if ((f = fopen("data/config.dat", "rb")) != NULL)
	{
		fread(&config, sizeof(config), 1, f);
		fclose(f);
	}
	
	/* allocate disk cache */
	disk = malloc(sizeof(struct _disk) * config.cache_size);
	
	/* load disk cache if available */
	bzero(disk, sizeof(struct _disk) * config.cache_size);
	if ((f = fopen("data/disk.dat", "rb")) != NULL)
	{
		fread(&disk_idx, sizeof(disk_idx), 1, f);
		fread(disk, sizeof(struct _disk), config.cache_size, f);
		fclose(f);
	}
	
	/* create disk cache directory if needed */
	mkdir("cache", 0755);
	
	/* load favorites if available */
	bzero(favorite, sizeof(favorite));
	if ((f = fopen("data/favorite.dat", "rb")) != NULL)
	{
		fread(favorite, sizeof(favorite), 1, f);
		fclose(f);
	}
	
	/* setup curl */
	curl = curl_easy_init();
	
	/* setup SDL */
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) == -1)
		quit();
	joystick = SDL_JoystickOpen(0);
	SDL_JoystickEventState(SDL_ENABLE);
	if (TTF_Init() == -1)
		quit();
	
	/* setup screen */
	flags = SDL_HWSURFACE | SDL_ANYFORMAT | SDL_DOUBLEBUF;
	screen = SDL_SetVideoMode(WIDTH, HEIGHT, BPP, flags);
	SDL_FillRect(screen, NULL, BLACK);
	prev = zoomSurface(screen, 1, 1, 0);
	next = zoomSurface(screen, 1, 1, 0);
	if (screen == NULL)
		quit();
	
	/* splash screen */
	logo = IMG_Load("data/contest.png");
	SDL_BlitSurface(logo, NULL, next, NULL);
	SDL_FreeSurface(logo);
	SDL_BlitSurface(next, NULL, screen, NULL);
	effect(FX_FADE);
	SDL_Delay(1500);
	
	/* load textures */
	logo = IMG_Load("data/logo.png");
	na = IMG_Load("data/na.png");
	zoom = IMG_Load("data/zoom.png");
	font = TTF_OpenFont("data/font.ttf", 11);
	
	/* display initial map */
	display(FX_FADE);
}

/* main loop */
void loop()
{
	int action;
	SDL_Event event;
	
	/* main loop */
	while (1)
	{
		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
				case SDL_QUIT:
					quit();
					break;
				case SDL_KEYDOWN:
				case SDL_JOYBUTTONDOWN:
					if (event.type == SDL_KEYDOWN)
						action = event.key.keysym.sym;
					else
						action = event.jbutton.button;
					switch (action)
					{
						case SDLK_LEFT:
						case PSP_BUTTON_LEFT:
							x -= DIGITAL_STEP;
							display(FX_LEFT);
							break;
						case SDLK_RIGHT:
						case PSP_BUTTON_RIGHT:
							x += DIGITAL_STEP;
							display(FX_RIGHT);
							break;
						case SDLK_UP:
						case PSP_BUTTON_UP:
							y -= DIGITAL_STEP;
							display(FX_UP);
							break;
						case SDLK_DOWN:
						case PSP_BUTTON_DOWN:
							y += DIGITAL_STEP;
							display(FX_DOWN);
							break;
						case SDLK_PAGEUP:
						case PSP_BUTTON_R:
							if (z > -4)
							{
								z--;
								x*=2;
								y*=2;
								display(FX_IN);
							}
							break;
						case SDLK_PAGEDOWN:
						case PSP_BUTTON_L:
							if (z < 16)
							{
								z++;
								x/=2;
								y/=2;
								display(FX_OUT);
							}
							break;
						case SDLK_F1:
						case PSP_BUTTON_Y:
							go();
							display(FX_FADE);
							break;
						case SDLK_F2:
						case PSP_BUTTON_X:
							s--;
							if (s < 0) s = NUM_VIEWS-1;
							display(FX_FADE);
							break;
						case SDLK_F3:
						case PSP_BUTTON_A:
							s++;
							if (s > NUM_VIEWS-1) s = 0;
							display(FX_FADE);
							break;
						case SDLK_F4:
						case PSP_BUTTON_B:
							config.show_info = !config.show_info;
							display(FX_NONE);
							break;
						case SDLK_ESCAPE:
						case PSP_BUTTON_START:
							menu();
							display(FX_FADE);
							break;
						default:
							break;
					}
					break;
			}
		}
		
		dx = SDL_JoystickGetAxis(joystick, 0);
		if (abs(dx) < JOYSTICK_DEAD) dx = 0; else dx -= abs(dx)/dx * JOYSTICK_DEAD;
		dx *= JOYSTICK_STEP / (32768 - JOYSTICK_DEAD);
		x += dx;
		
		dy = SDL_JoystickGetAxis(joystick, 1);
		if (abs(dy) < JOYSTICK_DEAD) dy = 0; else dy -= abs(dy)/dy * JOYSTICK_DEAD;
		dy *= JOYSTICK_STEP / (32768 - JOYSTICK_DEAD);
		y += dy;
		
		if (dx || dy) display(FX_NONE);
		
		SDL_Delay(50);
	}
}

int main(int argc, char *argv[])
{
	#ifdef _PSP_FW_VERSION
	pspDebugScreenInit();
	sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
	sceUtilityLoadNetModule(PSP_NET_MODULE_INET);
	netInit();
	SetupCallbacks();
	setupGu();
	netDialog();
	#endif
	init();
	loop();
	return 0;
}
