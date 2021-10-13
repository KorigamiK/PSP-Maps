#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <pthread.h>
#include <errno.h>

#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/io.h>

#include "gps.h"
#include "gpsdclient.h"

#include "minmea.h"

/******************************************************************/
enum unit (*GPSD_units)(void);
void (*GPSD_source_spec)(const char *fromstring,struct fixsource_t *source);

int (*GPS_open)(const char *, const char *,struct gps_data_t *);
int (*GPS_close)(struct gps_data_t *);
int (*GPS_read)(struct gps_data_t *);
int (*GPS_unpack)(char *, struct gps_data_t *);
bool (*GPS_waiting)(const struct gps_data_t *, int);
int (*GPS_stream)(struct gps_data_t *, unsigned int, void *);
const char *(*GPS_errstr)(const int);
void (*GPS_enable_debug)(int, FILE *);

/******************************************************************/
pthread_t gps_thread;
static pthread_mutex_t gps_mutex;

static struct gps_data_t gpsdata;
static float altfactor = METERS_TO_FEET;
static float speedfactor = MPS_TO_MPH;
static char *altunits = "ft";
static char *speedunits = "mph";
static struct fixsource_t source;
static int state = 0;		/* or MODE_NO_FIX=1, MODE_2D=2, MODE_3D=3 */
static float gps_latitude;
static float gps_longitude;
static float gps_altitude;

/******************************************************************/
/* NMEA-0183 standard baud rate */
#define BAUDRATE B4800

static char NMEA_device[256] = "/dev/ttyUSB0";
static FILE *NMEA_fp = NULL;

/******************************************************************/
static int openPort(const char *tty, int baud)
{
    int status;
    int fd;
    struct termios newtio;

    /* open the tty */
    fd = open(tty, O_RDWR | O_NOCTTY);
    if (fd < 0) {
	return fd;
    }

    /* flush serial port */
    status = tcflush(fd, TCIFLUSH);
    if (status < 0) {
	close(fd);
	return -1;
    }

    /* get current terminal state */
    tcgetattr(fd, &newtio);

    /* set to raw terminal type */
    newtio.c_cflag = baud | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNBRK | IGNPAR;
    newtio.c_oflag = 0;

    /* control parameters */
    newtio.c_cc[VMIN] = 1;	/* block for at least one charater */

    /* set its new attributes */
    status = tcsetattr(fd, TCSANOW, &newtio);
    if (status < 0) {
	close(fd);
	return -1;
    }
    return fd;
}

/******************************************************************/
int GPS_get_fix(float *latitude, float *longitude)
{ 
  int retval;
  // Lock the mutex
  pthread_mutex_lock(&gps_mutex);
  retval = state;
  // Copy the GPS data from protected zone to lat, long
  *latitude = gps_latitude;
  *longitude = gps_longitude;
  // Unlock the mutex
  pthread_mutex_unlock(&gps_mutex);
  
  return retval;
}

/******************************************************************/
int GPS_thread_quit(void)
{
  // Probably need to signal the gps thread to give it up before the join.

  /* We're done talking to gpsd. */
  if (strstr(NMEA_device, "gpsd"))
    (void)(*GPS_close)(&gpsdata);
  else
    pthread_kill(gps_thread,SIGHUP); // stop the fgets() with EINTR.
  
  if(pthread_join(gps_thread, NULL))
  {
    printf("Could not join thread\n");
    return -1;
  }

  // Clean up the mutex
  pthread_mutex_destroy(&gps_mutex);

  return 0;

}

/******************************************************************/
void* GPS_thread_run(void *arg)
{
  unsigned int flags = WATCH_ENABLE;
  int wait_clicks = 0;  /* cycles to wait before gpsd timeout */

  if (source.device != NULL)
    flags |= WATCH_DEVICE;
  (void)(*GPS_stream)(&gpsdata, flags, source.device);
  
  for (;;) {
    int c;
    
    /* wait 1/2 second for gpsd */
    if (!(*GPS_waiting)(&gpsdata, 500000)) {
      /* 240 tries at .5 Sec a try is a 2 minute timeout */
      if ( 240 < wait_clicks++ ) 
	pthread_exit(NULL); // die(GPS_TIMEOUT);
    } 
    else {
      wait_clicks = 0;
      errno = 0;
      if ((*GPS_read)(&gpsdata) == -1) {
	fprintf(stderr, "Socket error 4\n");
	pthread_exit(NULL); // die(errno == 0 ? GPS_GONE : GPS_ERROR);
      } 
      /* Here's where updates go now that things are established. */
      else {
	pthread_mutex_lock(&gps_mutex);            // Lock the mutex
	// Copy the GPS fix data to protected zone
	state = gpsdata.fix.mode;	  
	if ((gpsdata.fix.latitude==gpsdata.fix.latitude) &&
	    (gpsdata.fix.longitude==gpsdata.fix.longitude)) {
	  gps_latitude = gpsdata.fix.latitude;   /* Fill in the latitude. */
	  gps_longitude = gpsdata.fix.longitude; /* Fill in the longitude. */
	}
	pthread_mutex_unlock(&gps_mutex);          // Unlock the mutex
      }
    }
  }
  return NULL;
}

/******************************************************************/
int GPS_thread_init(void)
{
  (*GPSD_source_spec)(NULL, &source);
  fprintf(stderr, "Connecting to gpsd at %s:%s\n", source.server, source.port);

  /* Open the stream to gpsd. */
  if ((*GPS_open)(source.server, source.port, &gpsdata) != 0) {
    (void)fprintf(stderr,
		  "No gpsd running or network error: %d, %s\n",
		  errno, GPS_errstr(errno));
    return -1;
  }
  /*
  switch ((*GPSD_units)()) {
  case imperial:
    fprintf(stderr, "Using imperial units.\n");
    altfactor = METERS_TO_FEET;
    altunits = "ft";
    speedfactor = MPS_TO_MPH;
    speedunits = "mph";
    break;
  case nautical:
    fprintf(stderr, "Using nautical units.\n");
    altfactor = METERS_TO_FEET;
    altunits = "ft";
    speedfactor = MPS_TO_KNOTS;
    speedunits = "knots";
    break;
  case metric:
    fprintf(stderr, "Using metric units.\n");
    altfactor = 1;
    altunits = "m";
    speedfactor = MPS_TO_KPH;
    speedunits = "kph";
    break;
  default:
    fprintf(stderr, "Using default units.\n");
    break;
  }
  */
  if(pthread_create(&gps_thread, NULL, &GPS_thread_run, NULL))
  {
      fprintf(stderr,"Unable to spawn gpsd thread\n");
      return -1;
  }

  return 0;
}

/******************************************************************/
void* NMEA_thread_run(void *arg)
{
    char line[MINMEA_MAX_LENGTH];
    int i;
    while (fgets(line, sizeof(line), NMEA_fp) != NULL) {
        switch (minmea_sentence_id(line, false)) {
            case MINMEA_SENTENCE_RMC: {
                struct minmea_sentence_rmc frame;
                if (minmea_parse_rmc(&frame, line)) {
		  pthread_mutex_lock(&gps_mutex);            // Lock the mutex
		  // Copy the GPS fix data to protected zone
		  gps_latitude = minmea_tocoord(&frame.latitude);
		  gps_longitude = minmea_tocoord(&frame.longitude);
		  pthread_mutex_unlock(&gps_mutex);          // Unlock the mutex
                }
            } break;
            case MINMEA_SENTENCE_GGA: {
                struct minmea_sentence_gga frame;
                if (minmea_parse_gga(&frame, line)) {
		  pthread_mutex_lock(&gps_mutex);            // Lock the mutex
		  // Copy the GPS fix data to protected zone
		  state = frame.fix_quality+1;
		  pthread_mutex_unlock(&gps_mutex);          // Unlock the mutex
		}
            } break;
            default:
              break;
        }
    }
  return NULL;
}

/******************************************************************/
int GPS_load(void)
{
  void *handle = NULL;
  char *error;
  int retval = 1; // Hope for the best.  Assume success.
  int fd = -1;
  FILE *f;

  fprintf(stderr, "Creating gps mutex\n");

  // Initialize the mutex
  if(pthread_mutex_init(&gps_mutex, NULL))
  {
    fprintf(stderr,"Unable to initialize a mutex\n");
    return -1;
  }

  // First check for config file.  Then guess...
  if ((f = fopen("data/gps.dat", "r")) != NULL)
  {
    int baud = B4800;
    char buf[128];

    sprintf(NMEA_device, "/dev/ttyS1");
    while (fgets(buf, sizeof buf, f) != NULL)
    {
      //fprintf(stderr,"gps.dat - [%s]\n",buf);
      if (sscanf(buf, "device: %s", NMEA_device) == 1) 
	{}
      else if (sscanf(buf, "baud: %d", &fd) == 1) {
        switch (fd) {
        case 4800: 
          baud = B4800; break;
        case 9600: 
          baud = B9600; break;
        case 38400: 
          baud = B38400; break;
        }
      }
      // else we could also sscanf for "host", "port" for gpsd connection.
    }
    fclose(f);

    if (!strstr(NMEA_device, "gpsd")) {
      fprintf(stderr,"Attempting raw NMEA on %s at %d bps.\n", NMEA_device, fd);
      fd = openPort(NMEA_device, baud);
      retval = -3;
    }
  }

  if (retval == 1) {
    handle = dlopen("libgps.so.21", RTLD_LAZY);
    if (!handle) {
      fprintf(stderr, "%s\n", dlerror());
      retval = -3;
    }
  }

  if (handle)
  {
    dlerror();    /* Clear any existing error */

    *(void **) (&GPSD_units) = dlsym(handle, "gpsd_units");
    if ((error = dlerror()) != NULL) fprintf(stderr, "%s - gpsd_units\n", error);
    *(void **) (&GPSD_source_spec) = dlsym(handle, "gpsd_source_spec");
    if ((error = dlerror()) != NULL) fprintf(stderr, "%s - gpsd_source_specr\n", error);

    *(void **) (&GPS_open) = dlsym(handle, "gps_open");
    if ((error = dlerror()) != NULL) fprintf(stderr, "%s - gps_open\n", error);
    *(void **) (&GPS_close) = dlsym(handle, "gps_close");
    if ((error = dlerror()) != NULL) fprintf(stderr, "%s - gps_close\n", error);
    *(void **) (&GPS_read) = dlsym(handle, "gps_read");
    if ((error = dlerror()) != NULL) fprintf(stderr, "%s - gps_read\n", error);
    *(void **) (&GPS_unpack) = dlsym(handle, "gps_unpack");
    if ((error = dlerror()) != NULL) fprintf(stderr, "%s - gps_unpack\n", error);
    *(void **) (&GPS_waiting) = dlsym(handle, "gps_waiting");
    if ((error = dlerror()) != NULL) fprintf(stderr, "%s - gps_waiting\n", error);
    *(void **) (&GPS_stream) = dlsym(handle, "gps_stream");
    if ((error = dlerror()) != NULL) fprintf(stderr, "%s - gps_stream\n", error);
    *(void **) (&GPS_errstr) = dlsym(handle, "gps_errstr");
    if ((error = dlerror()) != NULL) fprintf(stderr, "%s - gps_errstr\n", error);
    *(void **) (&GPS_enable_debug) = dlsym(handle, "gps_enable_debug");
    if ((error = dlerror()) != NULL) fprintf(stderr, "%s - gps_enable_debug\n", error);

    printf("Found gpsd DLL.\n");

    if (GPS_thread_init()) {
      fprintf(stderr, "Skipping gpsd thread.\n");
      retval = -2;
    }
    else
    {
      sprintf(NMEA_device, "gpsd");
      return retval;
    }
  }

  // If no luck with gpsd, try raw NMEA.
  {
    if (fd < 0) {
      sprintf(NMEA_device, "/dev/ttyUSB0");
      fprintf(stderr,"Attempting raw NMEA on %s.\n", NMEA_device);
      fd = openPort(NMEA_device, B4800);
    }
    if (fd < 0) {
      sprintf(NMEA_device, "/dev/ttyUSB1");
      fprintf(stderr,"Attempting raw NMEA on %s.\n", NMEA_device);
      fd = openPort(NMEA_device, B4800);
    }
    if (fd < 0) {
      sprintf(NMEA_device, "/dev/ttyS1");
      fprintf(stderr,"Attempting raw NMEA on %s.\n", NMEA_device);
      fd = openPort(NMEA_device, B9600);
    }
    if (fd < 0) {
      sprintf(NMEA_device, "/dev/ttyS2");
      fprintf(stderr,"Attempting raw NMEA on %s.\n", NMEA_device);
      fd = openPort(NMEA_device, B9600);
    }
    if (fd < 0) {
      fprintf(stderr,"Failed to open port %s\n", NMEA_device);
      return -4;
    }
    else {
      fprintf(stderr,"Attempting raw NMEA on fileno %d.\n", fd);
      NMEA_fp = fdopen(fd, "rw");
    }
    if (!NMEA_fp){
      fprintf(stderr,"Failed to fdopen port %s\n", NMEA_device);
      return -4;
    }
    else {
      fprintf(stderr,"Launching NMEA thread.\n");
      if(pthread_create(&gps_thread, NULL, &NMEA_thread_run, NULL))
      {
	fprintf(stderr,"Unable to spawn NMEA thread\n");
	return -5;
      }
      else
	retval = 1;
    }
  }

  return retval;
}

