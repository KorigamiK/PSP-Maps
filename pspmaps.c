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

#include "global.h"
#include "kml.h"
#include "gmapjson.h"


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
//#include <SDL_mixer.h>
#include <curl/curl.h>

#if 1 /* ZIPIT_Z2 URL_DISABLING */
int DEFAULT_MAP = 0;
int DEFAULT_CHEAT_MAP = 18;
#else
#define DEFAULT_MAP 0
#define DEFAULT_CHEAT_MAP 18
#endif

#define BPP 32
#define BUFFER_SIZE 200 * 1024
#define MEMORY_CACHE_SIZE 32
#define DIGITAL_STEP 0.5
#define JOYSTICK_STEP 0.05
#define JOYSTICK_DEAD 10000
#define NUM_FAVORITES 99

#define BLACK SDL_MapRGB(screen->format, 0, 0, 0)
#define WHITE SDL_MapRGB(screen->format, 255, 255, 255)

#ifdef _PSP_FW_VERSION
#include <pspkernel.h>
#include <pspsdk.h>
#include <psputility.h>
#include <pspnet_apctl.h>
#include <motion.h>
#define printf pspDebugScreenPrintf
#define MODULE_NAME "PSP-Maps"
PSP_MODULE_INFO(MODULE_NAME, 0, 1, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
PSP_HEAP_SIZE_MAX();
void quit();
#include "netdialog.c"
#define DANZEFF_SDL
#include "pspctrl_emu.c"
#include "danzeff.c"
#include <pspusb.h>
#include <pspusbacc.h>
#include "pspusbgps.h"
#define PSP_USBGPS_DRIVERNAME "USBGps_Driver"
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
int motion_loaded, gps_loaded, dat_loaded = 0;
int netOff = 0;

/* x, y, z are in Google's format: z = [ -4 .. 16 ], x and y = [ 1 .. 2^(17-z) ] */
int z = 16, s = 0; // DEFAULT_MAP;
float x = 1, y = 1, dx, dy;
int active = 0, fav = 0, balancing = 0, cache_zoom = 3;

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

#ifdef GOOGLEMAPS_API2
/* this is the Google Maps API key used for address search
 * this one was created for a dummy domain
 * if it does not work, put your own Google Maps API key here */
char gkey[100] = "ABQIAAAAAslNJJmKiyq8-oFeyctp9xSFOvRczLyAyj57qAvViVrKq19E6hQUo2EXzTDJCL7m3VQT1DNUPzUWAw";
#else
///char locurl[1024] = "http://maps.google.com/maps/geo?output=csv&key=%s&q=%s";
//char locurl[1024] = "http://maps.googleapis.com/maps/api/geocode/xml?address=%s";
char locurl[1024] = "http://maps.googleapis.com/maps/api/geocode/json?address=%s";
#endif

/* user's configuration */
struct
{
	int cache_size;
	int use_effects;
	int show_info;
	int danzeff;
	int show_kml;
	int cheat;
	int follow_gps;
#if 1 /* ZIPIT_Z2 URL_DISABLING */
	int last_service;
#endif
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
	OS_MAPNIK,
	OS_CLOUDMADE,
	OS_OPENCYCLEMAP,
	OS_OPENCYCLEMAPTRANS,
	MQ_MAPQUEST,
	MQ_MAPQUESTAERIAL,
	NORMAL_VIEWS,
	GG_MOON_APOLLO,
	GG_MOON_CLEMBW,
	GG_MOON_ELEVATION,
	GG_MARS_VISIBLE,
	GG_MARS_ELEVATION,
	GG_MARS_INFRARED,
	GG_SKY_VISIBLE,
	GG_SKY_INFRARED,
	GG_SKY_MICROWAVE,
	GG_SKY_HISTORICAL,
	CHEAT_VIEWS
};

/* legend for view types */
char *_view[CHEAT_VIEWS] = {
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
	"Yahoo! Maps / Hybrid",
	"OpenStreetMap / Mapnik",
	"OpenStreetMap / CloudMade",
	"OpenCycleMap / Map",
	"OpenCycleMap / Transport",
	"MapQuest / Map",
	"MapQuest / Open Aerial",
	"",
	"Google Moon / Apollo",
	"Google Moon / Clem BW",
	"Google Moon / Elevation",
	"Google Mars / Visible",
	"Google Mars / Elevation",
	"Google Mars / Infrared",
	"Google Sky / Visible",
	"Google Sky / Infrared",
	"Google Sky / Microwave",
	"Google Sky / Historical",
};

/* urls for services, loaded from urls.txt */
char *_url[CHEAT_VIEWS];

/* PSP buttons list */
#ifdef GP2X
enum
{
	PSP_BUTTON_UP = 0,
	PSP_BUTTON_LEFT = 2,
	PSP_BUTTON_DOWN = 4,
	PSP_BUTTON_RIGHT = 6,
#ifdef ZIPIT_Z2
	PSP_BUTTON_SELECT = 7, 
	PSP_BUTTON_START = SDLK_TAB, // = 8
	PSP_BUTTON_X = SDLK_SPACE, // SDLK_TAB = 9
	PSP_BUTTON_R = SDLK_PERIOD,
	PSP_BUTTON_L = SDLK_COMMA,
	PSP_BUTTON_A = SDLK_RETURN, // 13
	PSP_BUTTON_Y = SDLK_HOME, // 44
	PSP_BUTTON_B = SDLK_END, //46
#else
	PSP_BUTTON_START = 8,
	PSP_BUTTON_SELECT = 9,
	PSP_BUTTON_L = 10,
	PSP_BUTTON_R = 11,
	PSP_BUTTON_Y = 12,
	PSP_BUTTON_A = 13,
	PSP_BUTTON_B = 14,
	PSP_BUTTON_X = 15,
#endif
	PSP_NUM_BUTTONS
};
#else
enum
{
	PSP_BUTTON_X,
	PSP_BUTTON_A,
	PSP_BUTTON_B,
	PSP_BUTTON_Y,
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
#endif

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
	MENU_DIRECTIONS,
	MENU_LOAD,
	MENU_SAVE,
	MENU_DEFAULT,
	MENU_INFO,
	MENU_KML,
	MENU_GPS,
	MENU_EFFECT,
#ifdef ZIPIT_Z2 /* ENABLE_OFFLINE_EXPORT */
	MENU_OFFLINE,
	MENU_CACHEZOOM,
	MENU_CACHESIZE,
	MENU_CACHEOUT,
	MENU_CHEAT,
#else
	MENU_KEYBOARD,
	MENU_CACHEZOOM,
	MENU_CACHESIZE,
	MENU_CHEAT,
	MENU_EXIT,
#endif
	MENU_QUIT,
	MENU_NUM
};

#ifdef ZIPIT_Z2 /* ENABLE_OFFLINE_EXPORT */
int cacheout = 1;
char *_cacheops[3] = {
	"clear",
	"copy to offline",
	"move to offline"
};
#endif

#if 1 /* ZIPIT_Z2 URL_DISABLING */
void next_service()
{
	do {
		s++;
		if (s > (config.cheat?CHEAT_VIEWS:NORMAL_VIEWS)-1) 
			s = (config.cheat?NORMAL_VIEWS+1:0);
	} while (_url[s][0] == '#');
}
	
void prev_service()
{
	do {
		s--;
		if (s < (config.cheat?NORMAL_VIEWS+1:0)) 
			s = (config.cheat?CHEAT_VIEWS:NORMAL_VIEWS)-1;
	} while (_url[s][0] == '#');
}
#endif

/* quit */
void quit()
{
	FILE *f;
	
	/* do not save .dat files if there were not loaded! */
	if (dat_loaded)
	{
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
#if 1 /* ZIPIT_Z2 URL_DISABLING */
			config.last_service = s;
#endif
			fwrite(&config, sizeof(config), 1, f);
			fclose(f);
		}
		
		/* save favorites */
		if ((f = fopen("data/favorite.dat", "wb")) != NULL)
		{
			fwrite(favorite, sizeof(favorite), 1, f);
			fclose(f);
		}
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
	{
	extern int GPS_thread_quit(void);
	printf("quit!\n");
	GPS_thread_quit();
	printf("done.\n");
	exit(0);
	}
	#endif
}

#include "tile.c"
#include "io.c"

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
				#if ! ( _PSP_FW_VERSION || GP2X )
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
				#if ! ( _PSP_FW_VERSION || GP2X )
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
				#if ! ( _PSP_FW_VERSION || GP2X )
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
				#if ! ( _PSP_FW_VERSION || GP2X )
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
				#if ! ( _PSP_FW_VERSION || GP2X )
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
				#if ! ( _PSP_FW_VERSION || GP2X )
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
				#if ! ( _PSP_FW_VERSION || GP2X )
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
#ifdef ZIPIT_Z2
	sprintf(temp, "Lat %7.3f | Lon %7.3f | %3.1d%% | %s", lat, lon, 100*(16-z)/20, _view[s]);
#else
	sprintf(temp, "Lat: %10.6f | Lon: %10.6f | Zoom: %3.1d%% | Type: %s", lat, lon, 100*(16-z)/20, _view[s]);
#endif
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
	if (config.show_kml) kml_display(screen, x, y, z);
	if (config.show_kml) gmapjson_display(screen, x, y, z);
	
	SDL_Flip(screen);
}

#ifdef ZIPIT_Z2
latin2utf8(char *src, char *dst, int max)
{
  int i;
  int j = 0;
  for (i = 0; i < strlen(src); i++)
  {
    /* 0xff00 would pass 8bit latin1, but ttf font wont.*/
    if ((src[i] & 0x80) == 0) // ASCII
      dst[j++] = src[i];
    else // Latin1 char.  Convert to UTF-8
    {
      dst[j++] = (0xC0 | (src[i] >>6));
      dst[j++] = (0x80 | (src[i] & 0x3F));
    }
  }
  dst[j] = '\0';
}
#endif

/* lookup address */
void go()
{
	char request[1024], buffer[50], *address;
	SDL_RWops *rw;
	int ret, code, precision;
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
#ifdef ZIPIT_Z2
	print(next, 50, HEIGHT/2 - 90, "Enter address: ");
#else
	print(next, 50, HEIGHT/2 - 90, "Enter address, up/down to change letters, start to validate: ");
#endif
	input(next, 50, HEIGHT/2 - 60, buffer, 46);
#ifdef ZIPIT_Z2
	/* Strip off trailing spaces */
	while (buffer[strlen(buffer)-1] == ' ')
	  buffer[strlen(buffer)-1] = 0;
	/* Convert Latin1 in URL to UTF-8 to conform to RFC 3986 from 2005. */
	latin2utf8(buffer, request, 100);

	DEBUG("address: %s\n", buffer);
	address = curl_escape(request, 0);
#else
	DEBUG("address: %s\n", buffer);
	address = curl_escape(buffer, 0);
#endif
	
#ifdef GOOGLEMAPS_API2
	sprintf(request, "http://maps.google.com/maps/geo?output=csv&key=%s&q=%s", gkey, address);
#else
	/* Google geocoding API V3 keys now require https, so try it without a key.*/
	sprintf(request, locurl, address);
#endif
	free(address);
	
	rw = SDL_RWFromMem(response, BUFFER_SIZE);
	
	//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
	curl_easy_setopt(curl, CURLOPT_URL, request);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, rw);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);
	curl_easy_perform(curl);
	
#ifdef GOOGLEMAPS_API2
	ret = sscanf(response, "%d,%d,%f,%f", &code, &precision, &lat, &lon);
	
	if (ret == 4 && code == 200)
	{
		DEBUG("precision: %d, lat: %f, lon: %f\n", precision, lat, lon);
		z = _zoom[precision];
		latlon2xy(lat, lon, &x, &y, z);
	}
#else
	gmapjson_location(response, &x, &y, &z);
#endif
	
	SDL_RWclose(rw);
}

/* calculate directions */
void directions()
{
	char request[1024], buffer[50], *departure, *destination;
	FILE *kml;
	
	box(next, WIDTH/2, HEIGHT/2 - 60, 400, 80, 200);
	print(next, 50, HEIGHT/2 - 90, "Enter departure address: ");
	input(next, 50, HEIGHT/2 - 60, buffer, 46);
#ifdef ZIPIT_Z2
	/* Strip off trailing spaces */
	while (buffer[strlen(buffer)-1] == ' ')
	  buffer[strlen(buffer)-1] = 0;
	/* Convert Latin1 in URL to UTF-8 to conform to RFC 3986 from 2005. */
	latin2utf8(buffer, request, 100);

	DEBUG("address: %s\n", buffer);
	departure = curl_escape(request, 0);
#else
	DEBUG("departure: %s\n", buffer);
	departure = curl_escape(buffer, 0);
#endif	
	box(next, WIDTH/2, HEIGHT/2 - 60, 400, 80, 200);
	print(next, 50, HEIGHT/2 - 90, "Enter destination address: ");
	input(next, 50, HEIGHT/2 - 60, buffer, 46);
#ifdef ZIPIT_Z2
	/* Strip off trailing spaces */
	while (buffer[strlen(buffer)-1] == ' ')
	  buffer[strlen(buffer)-1] = 0;
	/* Convert Latin1 in URL to UTF-8 to conform to RFC 3986 from 2005. */
	latin2utf8(buffer, request, 100);

	DEBUG("address: %s\n", buffer);
	destination = curl_escape(request, 0);
#else
	DEBUG("destination: %s\n", buffer);
	destination = curl_escape(buffer, 0);
#endif	
#ifdef GOOGLEMAPS_API2
	sprintf(request, "http://maps.google.com/maps?output=kml&saddr=%s&daddr=%s", departure, destination);
#else
	sprintf(request, "http://maps.googleapis.com/maps/api/directions/json?origin=%s&destination=%s", departure, destination);
#endif	
	free(departure);
	free(destination);
	
#ifdef GOOGLEMAPS_API2
	if ((kml = fopen("kml/route.kml", "w")) == NULL)
	{
		DEBUG("cannot open/create kml/route.kml\n");
		return;
	}
#else
	if ((kml = fopen("kml/route.json", "w")) == NULL)
	{
		DEBUG("cannot open/create kml/route.json\n");
		return;
	}
#endif
	
	//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
	curl_easy_setopt(curl, CURLOPT_URL, request);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, kml);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);
	curl_easy_perform(curl);
	
	fclose(kml);
	
	/* reload KML files */
	kml_free();
	kml_load();
	gmapjson_load();
	
	/* force KML display */
	config.show_kml = 1;
}

void menu_update(int cache_size)
{
	char temp[50];

	#ifdef GP2X
	#define MENU_LEFT 80
	#else
	#define MENU_LEFT 140
	#endif
	
	#define MENU_TOP 60
	#define MENU_BOTTOM 10
	#define MENU_Y (HEIGHT - MENU_TOP - MENU_BOTTOM) / MENU_NUM
	#define MAX_CACHEZOOM 9
	#define MAX_CACHESIZE 32 * 1024 * 100
	#define ENTRY(position, format...) sprintf(temp, format); print(next, MENU_LEFT, MENU_TOP + position * MENU_Y, temp);

	SDL_Rect pos;
	SDL_FillRect(next, NULL, BLACK);
	pos.x = MENU_LEFT-80;
	pos.y = 0;
	SDL_BlitSurface(logo, NULL, next, &pos);
#ifdef GOOGLEMAPS_API2
	print(next, MENU_LEFT+120, 10, "version " VERSION);
	print(next, MENU_LEFT+120, 25, "http://royale.zerezo.com/psp/");
	print(next, MENU_LEFT+120, 40, "http://github.com/GameMaker2k/");
#else
	// Original code has not kept up with google API changes, so...
	print(next, MENU_LEFT+120, 10, "version " VERSION);
	print(next, MENU_LEFT+120, 25, "https://github.com");
	print(next, MENU_LEFT+120, 40, " /korigamik/PSP-Maps");
#endif
	print(next, MENU_LEFT-20, MENU_TOP + active * MENU_Y, ">");
	ENTRY(MENU_VIEW, "Current view: %s", _view[s]);
	ENTRY(MENU_ADDRESS, "Enter address...");
	ENTRY(MENU_DIRECTIONS, "Get directions...");
	ENTRY(MENU_LOAD, "Load favorite: (%d) %s", fav+1, favorite[fav].name);
	ENTRY(MENU_SAVE, "Save favorite: (%d) %s", fav+1, favorite[fav].name);
	ENTRY(MENU_DEFAULT, "Default view");
	ENTRY(MENU_INFO, "Show informations: %s", config.show_info ? "Yes" : "No");
	ENTRY(MENU_KML, "Show KML data: %s", config.show_kml ? "Yes" : "No");
	ENTRY(MENU_GPS, "Center map on GPS: %s", config.follow_gps ? "Yes" : "No");
	ENTRY(MENU_EFFECT, "Transition effects: %s", config.use_effects ? "Yes" : "No");
#ifdef ZIPIT_Z2 /* ENABLE_OFFLINE_EXPORT */
	ENTRY(MENU_OFFLINE, "Offline only: %s", netOff ? "Yes" : "No");
	ENTRY(MENU_CACHEZOOM, "Cache zoom levels: %d", cache_zoom);
	ENTRY(MENU_CHEAT, "Switch to sky/moon/mars: %s", config.cheat ? "Yes" : "No");
	ENTRY(MENU_CACHESIZE, "Cache size: %d (~ %d MB)", cache_size, cache_size * 20 / 1000);
	ENTRY(MENU_CACHEOUT, "Cache: %s", _cacheops[cacheout]);
#else
	ENTRY(MENU_KEYBOARD, "Keyboard type: %s", config.danzeff ? "Danzeff" : "Arcade");
	ENTRY(MENU_CACHEZOOM, "Cache zoom levels: %d", cache_zoom);
	ENTRY(MENU_CHEAT, "Switch to sky/moon/mars: %s", config.cheat ? "Yes" : "No");
	ENTRY(MENU_CACHESIZE, "Cache size: %d (~ %d MB)", cache_size, cache_size * 20 / 1000);
	ENTRY(MENU_EXIT, "Exit menu");
#endif
	ENTRY(MENU_QUIT, "Quit PSP-Maps");
	SDL_BlitSurface(next, NULL, screen, NULL);
	SDL_Flip(screen);
}

void cache_resize(int cache_size)
{
	int i;
	int old;
	old = config.cache_size;
	printf("cache_resize(%d  => %d) idx = %d\n", old, cache_size, disk_idx);
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
		printf("Unlink cache file%d: %s\n", i, name);
		unlink(name);
		SDL_BlitSurface(next, NULL, screen, NULL);
		SDL_Flip(screen);
	}
	disk = realloc(disk, sizeof(struct _disk) * config.cache_size);
	/* clear newly allocated memory if needed */
	if (config.cache_size > old)
		bzero(&disk[old], sizeof(struct _disk) * (config.cache_size - old));
	else if (disk_idx >= config.cache_size)
		disk_idx = 0;
}

#ifdef ZIPIT_Z2 /* ENABLE_OFFLINE_EXPORT */
void mkpath(char *dir)
{
	char tmp[256];
	char *p = NULL;
	size_t len;
	
	snprintf(tmp, sizeof(tmp),"%s",dir);
	len = strlen(tmp);
	if(tmp[len - 1] == '/')
		tmp[len - 1] = 0;
	for(p = tmp + 1; *p; p++)
		if(*p == '/') {
			*p = 0;
			mkdir(tmp, S_IRWXU);
			*p = '/';
		}
	mkdir(tmp, S_IRWXU);
}

void export_cache(int mv)
{
	int i;
	FILE *infile, *outfile;
	char cachename[50];
	char name[256];
	char buffer[BUFFER_SIZE];

	box(next, WIDTH/2, HEIGHT/2, 400, 70, 200);
	print(next, 50, HEIGHT/2 - 30, "Extracting cache...");
	for (i = 0; i < disk_idx; i++)
	{
		int x = disk[i].x;
		int y = disk[i].y;
		int z = disk[i].z;
		int s = disk[i].s;
		float ratio = 1.0 * i / disk_idx;

		if ((_offline[s] == NULL) || !strlen(_offline[s]))
			continue;

		boxRGBA(next, WIDTH/2 - 180, HEIGHT/2, WIDTH/2 - 180 + 360.0 * ratio, HEIGHT/2 + 15, 255, 0, 0, 255);
		SDL_BlitSurface(next, NULL, screen, NULL);
		SDL_Flip(screen);

		diskname(cachename, i);
		sprintf(name, "offline/%s/%d/%d/", _offline[s], 17-z, x, y);
		mkpath(name); /* create folders if needed */
		sprintf(name, "offline/%s/%d/%d/%d.png", _offline[s], 17-z, x, y);
		if (mv && !rename(cachename, name)){
			printf("Rename cache file %d to %s\n", i, name);
			mv++;
			continue;
		}
		if ((infile = fopen(cachename, "rb")) != NULL) {
			if ((outfile = fopen(name, "wb")) != NULL)
			{
				int n;
				while (0 < (n = fread(buffer, 1, sizeof(buffer), infile)))
					fwrite(buffer, 1, n, outfile);
				fclose(outfile);
			}
			else printf("Cannot open %s\n",name);
			fclose(infile);
			if (mv) {// rename must have failed.  Try copy/unlink instead.
				unlink(cachename);
				mv++;
			}
		}
		else printf("Cannot read %s\n",cachename);

		printf("Export cache file %d to %s\n", i, name);
	}
	if (mv > 1) // Reset cache index if we actually moved ANY files.
		disk_idx = 0;
}
#endif

/* menu to load/save favorites */
void menu()
{
	SDL_Event event;
	int action, cache_size = config.cache_size;
	int i, j, k;
	
	#ifdef GP2X
	#define MENU_LEFT 80
	#else
	#define MENU_LEFT 140
	#endif
	
	#define MENU_TOP 60
	#define MENU_BOTTOM 10
	#define MENU_Y (HEIGHT - MENU_TOP - MENU_BOTTOM) / MENU_NUM
	#define MAX_CACHEZOOM 9
	#define MAX_CACHESIZE 32 * 1024 * 100
	#define ENTRY(position, format...) sprintf(temp, format); print(next, MENU_LEFT, MENU_TOP + position * MENU_Y, temp);
	
	menu_update(cache_size);
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
						case SDLK_LALT:
						case PSP_BUTTON_START:
							return;
#ifdef ZIPIT_Z2
							// PSP_BUTTON_X is set to SPACE on zipit.
#else
						case SDLK_SPACE:
#endif
						case PSP_BUTTON_B:
						case PSP_BUTTON_A:
						case PSP_BUTTON_Y:
						case PSP_BUTTON_X:
							switch (active)
							{
								/* enter address */
								case MENU_ADDRESS:
									go();
									return;
								/* get directions */
								case MENU_DIRECTIONS:
									directions();
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
									box(next, WIDTH/2, HEIGHT/2 - 60, 400, 80, 200);
									print(next, 50, HEIGHT/2 - 90, "Enter the name for this favorite: ");
									input(next, 50, HEIGHT/2 - 60, favorite[fav].name, 46);
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
									s = DEFAULT_MAP;
									return;
								/* infos */
								case MENU_INFO:
									config.show_info = !config.show_info;
									break;
								/* KML */
								case MENU_KML:
									config.show_kml = !config.show_kml;
									break;
								/* GPS */
								case MENU_GPS:
									config.follow_gps = !config.follow_gps;
									break;
								/* effects */
								case MENU_EFFECT:
									config.use_effects = !config.use_effects;
									break;
#ifdef ZIPIT_Z2 /* ENABLE_OFFLINE_EXPORT */
								/* Off the network */
								case MENU_OFFLINE:
									netOff = !netOff;
									if (!netOff) /* clear memory cache */
									  bzero(memory, sizeof(memory));
									break;
#else
								/* keyboard */
								case MENU_KEYBOARD:
									config.danzeff = !config.danzeff;
									break;
#endif
								/* zoom cache */
								case MENU_CACHEZOOM:
									box(next, WIDTH/2, HEIGHT/2, 400, 70, 200);
									print(next, 50, HEIGHT/2 - 30, "Loading zoom levels to cache...");
									int total = 0, done = 0;
									float xx = x, yy = y;
									for (k = 1; k <= cache_zoom; k++) total += pow(4, k+1);
									for (k = 1; k <= cache_zoom; k++)
									{
										xx *= 2;
										yy *= 2;
										for (j = yy-pow(2, k); j < yy+pow(2, k); j++)
										for (i = xx-pow(2, k); i < xx+pow(2, k); i++)
										{
											float ratio = 1.0 * ++done / total;
											boxRGBA(next, WIDTH/2 - 180, HEIGHT/2, WIDTH/2 - 180 + 360.0 * ratio, HEIGHT/2 + 15, 255, 0, 0, 255);
											/* special process for hybrid maps: get 2 images */
											switch (s)
											{
												case GG_HYBRID:
													gettile(i, j, z-k, GG_SATELLITE);
													break;
												case YH_HYBRID:
													gettile(i, j, z-k, YH_SATELLITE);
													break;
											}
											gettile(i, j, z-k, s);
											SDL_BlitSurface(next, NULL, screen, NULL);
											SDL_Flip(screen);

										}
									}
									break;
								/* cheat */
								case MENU_CHEAT:
									config.cheat = !config.cheat;
									s = DEFAULT_MAP;
									if (config.cheat) s = DEFAULT_CHEAT_MAP;
									break;
								/* disk cache */
								case MENU_CACHESIZE:
									if (config.cache_size != cache_size)
										cache_resize(cache_size);
									break;
								/* exit menu */
								case MENU_VIEW:
								/* view */
									return;
#ifdef ZIPIT_Z2 /* ENABLE_OFFLINE_EXPORT */
								case MENU_CACHEOUT:
									if (cacheout) {
										printf("Exporting cache...\n");
										export_cache(cacheout-1);
									}
									else {
										printf("wiping cache...\n");
										cache_resize(0);
										cache_resize(cache_size);
									}
									break;
#else
								case MENU_EXIT:
									return;
#endif
								/* quit PSP-Maps */
								case MENU_QUIT:
									quit();
							}
							menu_update(cache_size);
							break;
						case SDLK_LEFT:
						case PSP_BUTTON_LEFT:
						case PSP_BUTTON_L:
							switch (active)
							{
								/* view */
								case MENU_VIEW:
#if 1 /* ZIPIT_Z2 URL_DISABLING */
									prev_service();
#else
									s--;
									if (s < (config.cheat?NORMAL_VIEWS+1:0)) s = (config.cheat?CHEAT_VIEWS:NORMAL_VIEWS)-1;
#endif
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
								/* KML */
								case MENU_KML:
									config.show_kml = !config.show_kml;
									break;
								/* GPS */
								case MENU_GPS:
									config.follow_gps = !config.follow_gps;
									break;
								/* effects */
								case MENU_EFFECT:
									config.use_effects = !config.use_effects;
									break;
#ifdef ZIPIT_Z2 /* ENABLE_OFFLINE_EXPORT */
								/* Off the network */
								case MENU_OFFLINE:
									netOff = !netOff;
									if (!netOff) /* clear memory cache */
									  bzero(memory, sizeof(memory));
									break;
#else
								/* keyboard */
								case MENU_KEYBOARD:
									config.danzeff = !config.danzeff;
									break;
#endif
								/* zoom cache */
								case MENU_CACHEZOOM:
									cache_zoom--;
									if (cache_zoom < 1) cache_zoom = MAX_CACHEZOOM;
									break;
								/* cheat */
								case MENU_CHEAT:
									config.cheat = !config.cheat;
									s = DEFAULT_MAP;
									if (config.cheat) s = DEFAULT_CHEAT_MAP;
									break;
								/* disk cache */
								case MENU_CACHESIZE:
									cache_size /= 2;
									if (cache_size == 0) cache_size = MAX_CACHESIZE;
									if (cache_size < 100) cache_size = 0;
									break;
#ifdef ZIPIT_Z2 /* ENABLE_OFFLINE_EXPORT */
								case MENU_CACHEOUT:
									if (--cacheout < 0)	cacheout = 2;
									break;
#endif
							}
							menu_update(cache_size);
							break;
						case SDLK_RIGHT:
						case PSP_BUTTON_RIGHT:
						case PSP_BUTTON_R:
							switch (active)
							{
								/* view */
								case MENU_VIEW:
#if 1 /* ZIPIT_Z2 URL_DISABLING */
									next_service();
#else
									s++;
									if (s > (config.cheat?CHEAT_VIEWS:NORMAL_VIEWS)-1) s = (config.cheat?NORMAL_VIEWS+1:0);
#endif
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
								/* KML */
								case MENU_KML:
									config.show_kml = !config.show_kml;
									break;
								/* GPS */
								case MENU_GPS:
									config.follow_gps = !config.follow_gps;
									break;
								/* effects */
								case MENU_EFFECT:
									config.use_effects = !config.use_effects;
									break;
#ifdef ZIPIT_Z2 /* ENABLE_OFFLINE_EXPORT */
								/* Off the network */
								case MENU_OFFLINE:
									netOff = !netOff;
									if (!netOff) /* clear memory cache */
									  bzero(memory, sizeof(memory));
									break;
#else
								/* keyboard */
								case MENU_KEYBOARD:
									config.danzeff = !config.danzeff;
									break;
#endif
								/* zoom cache */
								case MENU_CACHEZOOM:
									cache_zoom++;
									if (cache_zoom > MAX_CACHEZOOM) cache_zoom = 1;
									break;
								/* cheat */
								case MENU_CHEAT:
									config.cheat = !config.cheat;
									s = DEFAULT_MAP;
									if (config.cheat) s = DEFAULT_CHEAT_MAP;
									break;
								/* disk cache */
								case MENU_CACHESIZE:
									cache_size *= 2;
									if (cache_size == 0) cache_size = 100;
									if (cache_size > MAX_CACHESIZE) cache_size = 0;
									break;
#ifdef ZIPIT_Z2 /* ENABLE_OFFLINE_EXPORT */
								case MENU_CACHEOUT:
									if (++cacheout > 2)	cacheout = 0;
									break;
#endif
							}
							menu_update(cache_size);
							break;
						case SDLK_UP:
						case PSP_BUTTON_UP:
							active--;
							if (active < 0) active = MENU_NUM-1;
							menu_update(cache_size);
							break;
						case SDLK_DOWN:
						case PSP_BUTTON_DOWN:
							active++;
							if (active > MENU_NUM-1) active = 0;
							menu_update(cache_size);
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
void init(int favNum)
{
	int flags;
	FILE *f;
	int i;
	char buffer[1024];
	
	/* clear memory cache */
	bzero(memory, sizeof(memory));
	
	/* default options */
	config.cache_size = 1600;
	config.use_effects = 1;
	config.show_info = 0;
	config.show_kml = 0;
	config.danzeff = 1;
	config.cheat = 0;
	config.follow_gps = 1;
#if 1 /* ZIPIT_Z2 URL_DISABLING */
	config.last_service = DEFAULT_MAP;
#endif
	
	/* load configuration if available */
	if ((f = fopen("data/config.dat", "rb")) != NULL)
	{
		fread(&config, sizeof(config), 1, f);
		fclose(f);
	}
	
	/* switch to sky if needed */
	if (config.cheat) s = DEFAULT_CHEAT_MAP;

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
	
	/* create kml directory if needed */
	mkdir("kml", 0755);
	
	/* load favorites if available */
	bzero(favorite, sizeof(favorite));
	if ((f = fopen("data/favorite.dat", "rb")) != NULL)
	{
		fread(favorite, sizeof(favorite), 1, f);
		fclose(f);
	}
	
	/* all .dat where loaded, we can save them on exit */
	dat_loaded = 1;
	
	/* setup curl */
	curl = curl_easy_init();
	
	/* setup SDL */
#ifdef ZIPIT_Z2
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) == -1) {
		printf("SDL_Init() failed.\n");
		quit();
	}
#else
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_AUDIO) == -1)
		quit();
#endif
	joystick = SDL_JoystickOpen(0);
	SDL_JoystickEventState(SDL_ENABLE);
	if (TTF_Init() == -1){
#ifdef ZIPIT_Z2
		printf("TTF_Init() failed.\n");
#endif
		quit();
	}
	//if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) < 0)
		//quit();
	
	/* load urls for services */
	if ((f = fopen("urls.txt", "r")) == NULL)
	{
#ifdef ZIPIT_Z2
		printf("cannot open urls file!\n");
#else
		DEBUG("cannot open urls file!\n");
#endif
		quit();
	}
	for (i = 0; i < CHEAT_VIEWS; i++) if (i != NORMAL_VIEWS)
	{
		if (fscanf(f, "%s", buffer) != 1)
		{
#ifdef ZIPIT_Z2
			printf("cannot read url for %s\n", _view[i]);
#else
			DEBUG("cannot read url for %s\n", _view[i]);
#endif
			quit();
		}
		_url[i] = malloc(strlen(buffer) + 1);
		strcpy(_url[i], buffer);
	}
	fclose(f);
#if 1 /* ZIPIT_Z2 URL_DISABLING */
	/* Pick some defaults that are NOT commented out in urls.txt file */
	if (_url[DEFAULT_MAP][0] == '#')
		for (i = 0; i < NORMAL_VIEWS; i++) {
			if (_url[DEFAULT_MAP][0] != '#') {
				DEFAULT_MAP = i; break; }
		}
	if (_url[DEFAULT_CHEAT_MAP][0] == '#')
		for (i = NORMAL_VIEWS+1; i < CHEAT_VIEWS; i++) {
			if (_url[DEFAULT_CHEAT_MAP][0] != '#') {
				DEFAULT_CHEAT_MAP = i; break; }
		}
	/* Set current service and DEFAULT to current service of last session. */
	s = config.last_service;
	s--; next_service(); /* decrement, then next_service ensures its good. */
	if (config.cheat) DEFAULT_CHEAT_MAP = s;
	else DEFAULT_MAP = s;
#endif
#ifdef GOOGLEMAPS_API2
#else
	/* load url for location lookup (so we can add a key later if needed) */
	if ((f = fopen("locurl.txt", "r")) != NULL)
	{
	  if (fscanf(f, "%s", buffer) != 1)
	    strncpy(locurl, buffer, 1023);
	  fclose(f);
	}
#endif
	
	#include "icon.xpm"
	SDL_WM_SetIcon(IMG_ReadXPMFromArray(icon_xpm), NULL);
	SDL_WM_SetCaption("PSP-Maps " VERSION, "PSP-Maps " VERSION);
	SDL_ShowCursor(SDL_DISABLE);
	
	/* setup screen */
	flags = SDL_HWSURFACE | SDL_ANYFORMAT | SDL_DOUBLEBUF;
	screen = SDL_SetVideoMode(WIDTH, HEIGHT, BPP, flags);
	SDL_FillRect(screen, NULL, BLACK);
	#ifdef GP2X
	prev = SDL_ConvertSurface(screen, screen->format, screen->flags);
	next = SDL_ConvertSurface(screen, screen->format, screen->flags);
	#else
	prev = zoomSurface(screen, 1, 1, 0);
	next = zoomSurface(screen, 1, 1, 0);
	#endif
	if (screen == NULL) {
#ifdef ZIPIT_Z2
		printf("SDL_VideoMode() failed.\n");
#endif
		quit();
	}
	/* load textures */
	logo = IMG_Load("data/logo.png");
	na = IMG_Load("data/na.png");
	zoom = IMG_Load("data/zoom.png");
	font = TTF_OpenFont("data/font.ttf", 11);
	
	/* load KML */
	kml_load();
	gmapjson_load();
	
	if ((favNum >= 0) && (favNum < NUM_FAVORITES) &&  favorite[favNum].ok)
	{
	  fav = favNum;
	  x = favorite[fav].x;
	  y = favorite[fav].y;
	  z = favorite[fav].z;
	  s = favorite[fav].s;
	}

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
						case SDLK_BACKSPACE:
							{static int offset=0;
							offset = show_rtf(screen,"data/font.ttf","/tmp/route.rtf",offset);
							display(FX_NONE);
							}
							break;
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
						// case PSP_BUTTON_UP:
							y -= DIGITAL_STEP;
							display(FX_UP);
							break;
						case SDLK_DOWN:
						case PSP_BUTTON_DOWN:
							y += DIGITAL_STEP;
							display(FX_DOWN);
							break;
						case SDLK_PAGEUP:
						case SDLK_RCTRL:
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
						case SDLK_RALT:
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
						case SDLK_LCTRL:
						case PSP_BUTTON_X:
							go();
							display(FX_FADE);
							break;
						case SDLK_F2:
#ifdef ZIPIT_Z2
							// PSP_BUTTON_Y is set to HOME on zipit.
#else
						case SDLK_HOME:
#endif
						case PSP_BUTTON_Y:
#if 1 /* ZIPIT_Z2 URL_DISABLING */
							prev_service();
#else
							s--;
							if (s < (config.cheat?NORMAL_VIEWS+1:0)) s = (config.cheat?CHEAT_VIEWS:NORMAL_VIEWS)-1;
#endif
							display(FX_FADE);
							break;
						case SDLK_F3:
#ifdef ZIPIT_Z2
							// PSP_BUTTON_B is set to END on zipit.
#else
						case SDLK_END:
#endif
						case PSP_BUTTON_B:
#if 1 /* ZIPIT_Z2 URL_DISABLING */
							next_service();
#else
							s++;
							if (s > (config.cheat?CHEAT_VIEWS:NORMAL_VIEWS)-1) s = (config.cheat?NORMAL_VIEWS+1:0);
#endif
							display(FX_FADE);
							break;
						case SDLK_i:
						case SDLK_F4:
						case PSP_BUTTON_A:
							config.show_info = !config.show_info;
							display(FX_NONE);
							break;
						case SDLK_ESCAPE:
						case SDLK_LALT:
						case PSP_BUTTON_START:
							menu();
							display(FX_FADE);
							break;
						case SDLK_c:
                                                        config.follow_gps = !config.follow_gps;
							break;
						case SDLK_d:
                                                        directions();
							break;
						case SDLK_g:
                                                        go();
							break;
						case SDLK_k:
                                                        config.show_kml = !config.show_kml;
							display(FX_NONE);
							break;
						case SDLK_o:
                                                        netOff = !netOff;
							if (!netOff) /* clear memory cache */
							  bzero(memory, sizeof(memory));
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
		
		dy = SDL_JoystickGetAxis(joystick, 1);
		if (abs(dy) < JOYSTICK_DEAD) dy = 0; else dy -= abs(dy)/dy * JOYSTICK_DEAD;
		dy *= JOYSTICK_STEP / (32768 - JOYSTICK_DEAD);
		
		#ifdef _PSP_FW_VERSION
		if (motion_loaded && motionExists())
		{
			motionAccelData accel;
			motionGetAccel(&accel);
			dx -= (accel.x - 128) / 200.0;
			dy += (accel.y - 128) / 200.0;
			if (accel.z < 140)
			if (z > -4)
			{
				z--;
				x*=2;
				y*=2;
				display(FX_IN);
			}
			if (accel.z > 180)
			if (z < 16)
			{
				z++;
				x/=2;
				y/=2;
				display(FX_OUT);
			}
		}
		
		if (gps_loaded && config.follow_gps)
		{
			gpsdata gpsd;
			satdata satd;
			bzero(&gpsd, sizeof(gpsd));
			bzero(&satd, sizeof(satd));
			sceUsbGpsGetData(&gpsd, &satd);
			if (gpsd.latitude != 0 || gpsd.longitude != 0)
			{
				latlon2xy(gpsd.latitude, gpsd.longitude, &x, &y, z);
				dx = 0;
				dy = 0;
				display(FX_NONE);
			}
		}
		#else
		if (gps_loaded && config.follow_gps)
		{
			extern int GPS_get_fix(float *latitude, float *longitude);
                        static int gps_ctr = 0;
			float latitude, longitude;
                        gps_ctr++;
                        gps_ctr &= 0xf; // For get_fix: 0 = no connection, 1 = no fix.
			if ((gps_ctr == 0) && (GPS_get_fix(&latitude, &longitude) > 1))
			{
                                static float save_lat = 0.0;
                                static float save_lon = 0.0;
                                if ((save_lat != latitude) || (save_lon != longitude)) {
                                    save_lat = latitude;
                                    save_lon = longitude;
                                    latlon2xy(latitude, longitude, &x, &y, z);
                                    dx = 0;
                                    dy = 0;
                                    display(FX_NONE);
                                }
			}

		}
		#endif
		
		x += dx;
		y += dy;
		
		if (dx || dy) display(FX_NONE);
		
		SDL_Delay(50);
	}
}

#ifdef _PSP_FW_VERSION
int gpsLoad()
{
	if (sceUtilityLoadUsbModule(PSP_USB_MODULE_ACC))
		return -1;
	if (sceUtilityLoadUsbModule(PSP_USB_MODULE_GPS))
		return -2;
	if (sceUsbStart(PSP_USBBUS_DRIVERNAME, 0, 0))
		return -3;
	if (sceUsbStart(PSP_USBACC_DRIVERNAME, 0, 0))
		return -4;	
	if (sceUsbStart(PSP_USBGPS_DRIVERNAME, 0, 0))
		return -5;
	if (sceUsbGpsOpen())
		return -6;
	if (sceUsbActivate(PSP_USBGPS_PID))
		return -7;
	return 0;
}
#endif

int main(int argc, char *argv[])
{
  int i;
  int favNum = -1;
  char *favNam = NULL;
  for (i = 1; i < argc; i++) {
    if (argv[i][0] == '-') {
      switch (argv[i][1])
      {
      case 'o':
	netOff = 1;
	break;
      case 'f':
	if (isdigit(argv[i][2]))
	  favNum = atoi(&(argv[i][2]));
	else if (++i < argc)
	  favNum = atoi(argv[i]);
	favNum--; // Convert from 1 based to 0 based.
	break;
      }
    }
    else {
      favNam = argv[i];
    }
  } 

	#ifdef _PSP_FW_VERSION
	pspDebugScreenInit();
	motion_loaded = motionLoad() >= 0;
	gps_loaded = gpsLoad() >= 0;
	sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
	sceUtilityLoadNetModule(PSP_NET_MODULE_INET);
	netInit();
	SetupCallbacks();
	setupGu();
	netDialog();
	#else
	extern int GPS_load(void);
	gps_loaded = GPS_load() >= 0;
	#endif
	init(favNum);
	loop();
	return 0;
}
