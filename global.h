#define VERSION "2.4.3"

#ifdef GP2X
#define WIDTH 320
#define HEIGHT 240
#else
#define WIDTH 480
#define HEIGHT 272
#endif

#if ZIPIT_Z2_DEBUG
#define DEBUG(x...) printf(x);
#else
#if _PSP_FW_VERSION || GP2X 
#define DEBUG(x...) {}
#else
#define DEBUG(x...) printf(x);
#endif
#endif

void latlon2xy(float lat, float lon, float *x, float *y, int z);
