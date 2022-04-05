#ifndef IMAGEDATA_H
#define IMAGEDATA_H
#include <netpbm/pgm.h>
#include <netpbm/ppm.h>

/* Scanned image descriptor.  */
struct image_data
{
  /* Grayscale scan.  */
  gray **data;
  /* Optional color scan.  */
  pixel **rgbdata;
  /* Dimensions of image data.  */
  int width, height;
  /* Maximal value of the image data.  */
  gray maxval;

  /* Load image data from PNM file.  */
  bool load_pnm (FILE *graydata, FILE *colordata, const char **error);
};
#endif
