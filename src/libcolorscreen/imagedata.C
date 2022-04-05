#include "include/imagedata.h"
bool
image_data::load_pnm (FILE *grayfile, FILE *colorfile, const char **error)
{
  data = pgm_readpgm (grayfile, &width, &height, &maxval);
  rgbdata = NULL;
  if (!data)
    {
      *error = "Failed to read scan";
      return false;
    }
  gamma=2.2;
  if (colorfile)
    {
      int rgb_width, rgb_height;
      pixval rgb_maxval;

      rgbdata = ppm_readppm (colorfile, &rgb_width, &rgb_height, &rgb_maxval);
      if (!rgbdata)
	{
	  *error = "Failed to read RGB scan";
	  return false;
	}
      if (width != rgb_width || height != rgb_height)
	{
	  *error = "scan and RGB scan must have same dimensions";
	  return false;
	}
      if (maxval != rgb_maxval)
	{
	  *error = "scan and RGB scan must have same bit depth";
	  return false;
	}
    }
  return true;
}
