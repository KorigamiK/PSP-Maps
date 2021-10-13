#include "global.h"
#include "kml.h"

#include <math.h>
#include <string.h>
#include <dirent.h>
#include <SDL_image.h>
#include <SDL_gfxPrimitives.h>

#include "cJSON.h"

// Try to share internal data structs with kml code.
extern SDL_Surface *marker;
extern Placemark *places;
extern char txtbuf[];

// Bounding box to zoom level (globe = 256 pixels at no zoom).
#define GLOBE_WIDTH 256
#define LN2 0.693147181f

int DecodePolylinePoints(char* polylinechars, Placemark * place, int zoom)
{
  char content[128];
  int i = 0;
  
  int currentLat = 0;
  int currentLng = 0;
  int next5bits;
  int sum;
  int shifter;
  int n;

  if (polylinechars == NULL || strlen(polylinechars) == 0) 
    return 0;
  n = strlen(polylinechars);
  while (i < n)
  {
    // calculate next latitude
    sum = 0;
    shifter = 0;
    do
    {
      next5bits = (int)polylinechars[i++] - 63;
      sum |= (next5bits & 31) << shifter;
      shifter += 5;
    } while (next5bits >= 32 && i < n);
    
    if (i >= n)
      break;
	  
    currentLat += (sum & 1) == 1 ? ~(sum >> 1) : (sum >> 1);
	  
    //calculate next longitude
    sum = 0;
    shifter = 0;
    do
    {
      next5bits = (int)polylinechars[i++] - 63;
      sum |= (next5bits & 31) << shifter;
      shifter += 5;
    } while (next5bits >= 32 && i < n);
	  
    if (i >= n && next5bits >= 32)
      break;
    
    currentLng += (sum & 1) == 1 ? ~(sum >> 1) : (sum >> 1);

    sprintf(content, "%f,%f,%f ", (double)(currentLng) / 100000.0,(double)(currentLat) / 100000.0, 0.0f); // zoom?

    place->line = realloc(place->line, strlen(place->line) + strlen(content) + 1);
    strcat(place->line, (char *) content);
  } 
  return 1;
}

void location_parse(cJSON *location, cJSON *address)
{
  Placemark *place = malloc(sizeof(Placemark));

  /* parse point */
  place->type = PLACEMARK_POINT;
  place->marker = marker;
  place->point = malloc(sizeof(PlacemarkPoint));
  place->point->lon = cJSON_GetObjectItem(location, "lng")->valuedouble;
  place->point->lat = cJSON_GetObjectItem(location, "lat")->valuedouble;
  if (address)
    place->name = strdup((char *) address->valuestring);
  place->next = places;
  places = place;
}

void bounds_parse(cJSON *bounds, int *zoom)
{
	if (bounds) {
		double east = 0.0;
		double west = 0.0;
		double angle;
		int z;
		cJSON * northeast = cJSON_GetObjectItem(bounds, "northeast");
		cJSON * southwest = cJSON_GetObjectItem(bounds, "southwest");
		if (northeast)
		    east = cJSON_GetObjectItem(northeast, "lng")->valuedouble;
		if (southwest)
		    west = cJSON_GetObjectItem(southwest, "lng")->valuedouble;
		// Convert bounding box to zoom level.
		angle = east - west;
		if (angle < 0.0f)
		    angle += 360.0f;
		z = 18 - round(log(WIDTH * 360.0f / angle / GLOBE_WIDTH) / LN2);
		if (z > 16) z = 16; // global
		if (z < 1) z = 1; // exact street address
		*zoom = z;
	}
}

char *html2txt(char *s)
{
	static char buf[1032];
	int i,j,k;
	char *p,*q;

	strncpy(buf,s,1031);
	buf[1031] = 0;
	p = buf;
	while (p = strchr(p, '<')){
		if (q = strchr(p, '>')){
			if (!strncmp(p,"<div",4)){
				strcpy(p, "\n(");
				p+=2;
			}
			if (!strncmp(p,"</div",5)){
				strcpy(p, ") ");
				p+=2;
			}
			*q++ = 0;
			strcpy(p,q);
		}
		else break;
	}
	return(buf);
}

void dist_parse(cJSON * step)
{
	char *s = txtbuf+strlen(txtbuf);
	cJSON * dist = cJSON_GetObjectItem(step, "distance");
	cJSON * time = cJSON_GetObjectItem(step, "duration");
	if (dist)
		if (dist = cJSON_GetObjectItem(dist, "text"))
			sprintf(s," %s\n",dist->valuestring);
	if (time) 
		if (time = cJSON_GetObjectItem(time, "text"))
			sprintf(s+strlen(s)-1, "  --  %s\n", time->valuestring);
}

void gmapjson_parse(char *file)
{
  int i,j,k, zoom=0;
	cJSON *json = NULL;
	cJSON *status = NULL;
	cJSON *routes = NULL;
	cJSON *bounds = NULL;
	cJSON *legs = NULL;
	cJSON *overview_polyline = NULL;
	FILE *f;
	char *text;
	if (f=fopen(file,"rb")) {
		long len;
		fseek(f,0,SEEK_END); len=ftell(f); fseek(f,0,SEEK_SET);
		text=(char*)malloc(len+1); fread(text,1,len,f);
		fclose(f);
	}
	else return;
	json=cJSON_Parse(text);
	if (!json) return;
	status = cJSON_GetObjectItem(json, "status"); 
	routes = cJSON_GetObjectItem(json,"routes");
	if (status && (!strcmp(status->valuestring, "OK")) &&
	    routes && (cJSON_GetArraySize(routes) > 0)) {
		// Use the first (best?) route, ignore others.
		cJSON *route = cJSON_GetArrayItem(routes, 0);
		bounds = cJSON_GetObjectItem(route, "bounds");
		legs = cJSON_GetObjectItem(route, "legs");
		overview_polyline = cJSON_GetObjectItem(route, "overview_polyline");
	}
	if (bounds) 
	  bounds_parse(bounds, &zoom);
	// Should be one and only one leg because thats what we requested.
	if (legs) 
	  for (j = 0 ; j < cJSON_GetArraySize(legs) ; j++) {
		char *s = txtbuf+strlen(txtbuf);
		cJSON * leg = cJSON_GetArrayItem(legs, j);
		cJSON * start_location = cJSON_GetObjectItem(leg, "start_location");
		cJSON * start_address = cJSON_GetObjectItem(leg, "start_address");
		cJSON * end_location = cJSON_GetObjectItem(leg, "end_location");
		cJSON * end_address = cJSON_GetObjectItem(leg, "end_address");
		cJSON * dist = cJSON_GetObjectItem(leg, "distance");
		cJSON * time = cJSON_GetObjectItem(leg, "duration");
		if (start_location && start_address)
		    location_parse(start_location, start_address);
		if (end_location && end_address)
		    location_parse(end_location, end_address);
		if (start_address && end_address) {
			sprintf(s,"%s\n",start_address->valuestring);
			sprintf(s+strlen(s),"to\n%s\n",end_address->valuestring);
			dist_parse(leg);
			strcat(s+strlen(s), " \n");
			//printf(s);
		}
		cJSON *steps = cJSON_GetObjectItem(leg, "steps");
		if (steps) 
		  for (k = 0 ; k < cJSON_GetArraySize(steps) ; k++) {
				s = txtbuf+strlen(txtbuf);
		    cJSON * step = cJSON_GetArrayItem(steps, k);
		    cJSON * inst = cJSON_GetObjectItem(step, "html_instructions");
				if (inst) sprintf(s,"%s\n",html2txt(inst->valuestring));
				dist_parse(step);
				//printf(s);
			}
		}
	if (overview_polyline) {
		cJSON * points = cJSON_GetObjectItem(overview_polyline, "points");
		if (points){
		Placemark *place = malloc(sizeof(Placemark));
		place->type = PLACEMARK_LINE;
		place->line = strdup("");
		DecodePolylinePoints(points->valuestring, place, zoom);
		place->next = places;
		places = place;
		}
	}
	cJSON_Delete(json);
	free(text);
}

void gmapjson_location(char *text, float *x, float *y, int *z)
{
	float lat = 0.0f, lon = 0.0f;
	cJSON *json = NULL;
	cJSON *status = NULL;
	cJSON *results = NULL;
	cJSON *bounds = NULL;
	cJSON *location = NULL;
	//cJSON *address = NULL;
	json=cJSON_Parse(text);
	if (!json) return;
	status = cJSON_GetObjectItem(json, "status"); 
	results = cJSON_GetObjectItem(json,"results");
	if (status && (!strcmp(status->valuestring, "OK")) &&
	    results && (cJSON_GetArraySize(results) > 0)) {
		// Use the first (best?) result, ignore others.
		cJSON *result = cJSON_GetArrayItem(results, 0);
		cJSON *geometry = cJSON_GetObjectItem(result, "geometry");
		if (geometry) {
			bounds = cJSON_GetObjectItem(geometry, "bounds");
			if (!bounds) 
				bounds = cJSON_GetObjectItem(geometry, "viewport");
			location = cJSON_GetObjectItem(geometry, "location");
		}
		//address = cJSON_GetObjectItem(result, "formatted_address");
	}
	if (location)  {
		*z = 9; //subregion
		if (bounds) 
			bounds_parse(bounds, z);
		lon = cJSON_GetObjectItem(location, "lng")->valuedouble;
		lat = cJSON_GetObjectItem(location, "lat")->valuedouble;
		latlon2xy(lat, lon, x, y, *z);
	}
	// Put a pin a the location?
	//if (address) name = strdup((char *) address->valuestring);
	cJSON_Delete(json);
}

void gmapjson_load()
{
	DIR *directory;
	struct dirent *entry;
	char file[100];
	SDL_Surface *default_marker;

	/* default marker */
	default_marker = IMG_Load("data/marker.png");
	
	/* parse the kml directory */
	if ((directory = opendir("kml/")) != NULL)
		while ((entry = readdir(directory)) != NULL)
			/* load only the .json files */
			if (entry->d_name[0] != '.' && strcasecmp(strchr(entry->d_name, '.'), ".json") == 0)
			{
				/* remove .kml suffix */
				strchr(entry->d_name, '.')[0] = '\0';
				/* try to load .png image */
				sprintf(file, "kml/%s.png", entry->d_name);
				DEBUG("gmapjson_parse(\"%s\")\n", file);
				marker = IMG_Load(file);
				/* if not available, default marker */
				if (marker == NULL) marker = default_marker;
				/* load KML file */
				sprintf(file, "kml/%s.json", entry->d_name);
				gmapjson_parse(file);
			}

	rtf_update();
}

fatLineColor(SDL_Surface *dst, int x1, int y1, int x2, int y2, int color)
{
	Sint16 x[4];
	Sint16 y[4];
	int dx=0; int dy=0;

	if(abs(x1-x2) > abs(y1-y2))
		dy=2;
	else
		dx=2;
	x[0] = x1+dx;
	x[1] = x1-dx;
	x[2] = x2-dx;
	x[3] = x2+dx;
	y[0] = y1+dy;
	y[1] = y1-dy;
	y[2] = y2-dy;
	y[3] = y2+dy;
	filledPolygonColor(dst, x, y, 4, color);
}

void gmapjson_display(SDL_Surface *dst, float x, float y, int z)
{
	Placemark *place = places;
	float nx, ny;

	while (place)
	{
		switch (place->type)
		{
			case PLACEMARK_POINT:
			{
				latlon2xy(place->point->lat, place->point->lon, &nx, &ny, z);
				SDL_Rect pos;
				pos.x = (nx-x)*256+WIDTH/2 - place->marker->w/2;
				pos.y = (ny-y)*256+HEIGHT/2 - place->marker->h/2;
				SDL_BlitSurface(place->marker, NULL, dst, &pos);
				break;
			}
			case PLACEMARK_LINE:
			{
				int init = 0;
				float ox = 0, oy = 0;
				char *copy = strdup(place->line);
				char *tmp;
				tmp = strtok(copy, " ");
				while (tmp)
				{
					float lat, lon;
					sscanf(tmp, "%f,%f,", &lon, &lat);
					latlon2xy(lat, lon, &nx, &ny, z);
					if (init)
						//lineColor(dst, (ox-x)*256+WIDTH/2, (oy-y)*256+HEIGHT/2, (nx-x)*256+WIDTH/2, (ny-y)*256+HEIGHT/2, 0x0000ffaa);
						//aalineColor(dst, (ox-x)*256+WIDTH/2, (oy-y)*256+HEIGHT/2, (nx-x)*256+WIDTH/2, (ny-y)*256+HEIGHT/2, 0x0000ffaa);
						//thickLineColor(dst, (ox-x)*256+WIDTH/2, (oy-y)*256+HEIGHT/2, (nx-x)*256+WIDTH/2, (ny-y)*256+HEIGHT/2, 2, 0x0000ffaa);
						fatLineColor(dst, (ox-x)*256+WIDTH/2, (oy-y)*256+HEIGHT/2, (nx-x)*256+WIDTH/2, (ny-y)*256+HEIGHT/2, 0x0000ffaa);
					ox = nx;
					oy = ny;
					init = 1;
					tmp = strtok(NULL, " ");
				}
				free(copy);
				break;
			}
		}
		place = place->next;
	}
}
