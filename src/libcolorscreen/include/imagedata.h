#ifndef IMAGEDATA_H
#define IMAGEDATA_H

/* Scanned image descriptor.  */
class image_data
{
public:
  typedef unsigned short gray;
  struct pixel
  {
    gray r, g, b;
  };
  /* Grayscale scan.  */
  gray **data;
  /* Optional color scan.  */
  pixel **rgbdata;

  image_data ()
  : data (NULL), rgbdata (NULL), width (0), height (0), maxval (0), own (false)
  { }
  ~image_data ();
  /* Dimensions of image data.  */
  int width, height;
  /* Maximal value of the image data.  */
  gray maxval;

  /* Load image data from TIFF file.  */
  bool load_tiff (const char *name, const char **error);
  /* Load image data from JPG file.  */
  bool load_jpg (const char *name, const char **error);
  /* Load image data from file with auto-detection.  */
  bool load (const char *name, const char **error);
  bool allocate (bool rgb);
private:
  /* True of the data is owned by the structure.  */
  bool own;
};
#endif
