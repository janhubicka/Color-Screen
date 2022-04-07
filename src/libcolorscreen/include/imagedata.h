#ifndef IMAGEDATA_H
#define IMAGEDATA_H
#include <netpbm/pgm.h>
#include <netpbm/ppm.h>

/* Scanned image descriptor.  */
class image_data
{
public:

  image_data ()
  : data (NULL), rgbdata (NULL), width (0), height (0), maxval (0), own (false)
  { }
  ~image_data ()
  {
    if (!own)
      return;
    if (data)
      {
	free (*data);
	free (data);
      }
    if (rgbdata)
      {
	free (*rgbdata);
	free (rgbdata);
      }
  }
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
  bool load_tiff (const char *name, const char **error);
  bool allocate (bool rgb);
private:
  /* True of the data is owned by the structure.  */
  bool own;
};
#endif
