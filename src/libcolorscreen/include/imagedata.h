#ifndef IMAGEDATA_H
#define IMAGEDATA_H
#include "dllpublic.h"
#include "color.h"

class image_data_loader;

/* Scanned image descriptor.  */
class DLL_PUBLIC image_data
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
  : data (NULL), rgbdata (NULL), width (0), height (0), maxval (0), id (last_imagedata_id), loader (NULL), own (false)
  { 
    last_imagedata_id++;
  }
  ~image_data ();
  /* Dimensions of image data.  */
  int width, height;
  /* Maximal value of the image data.  */
  int maxval;
  /* Unique id of the image (used for caching).  */
  int id;


#if 0
  /* Load image data from TIFF file.  */
  bool load_tiff (const char *name, const char **error);
  /* Load image data from JPG file.  */
  bool load_jpg (const char *name, const char **error);
#endif
  /* Initialize loader for NAME.  Return true on success.
     If false is returned ERROR is initialized to error
     message.  */
  bool init_loader (const char *name, const char **error);
  /* True if grayscale allocation is needed
     (used after init_loader and before load_part).  */
  bool allocate_grayscale ();
  /* True if rgballocation is needed
     (used after init_loader and before load_part).  */
  bool allocate_rgb ();
  /* Load part of image. Initialize PERMILLE to status.
     If PERMILLE==1000 loading is finished.
     If false is returned ERROR is initialized.  */
  bool load_part (int *permille, const char **error);

  /* Allocate memory.  */
  bool allocate ();
  /* Load image data from file with auto-detection.  */
  bool load (const char *name, const char **error);
private:
  image_data_loader *loader;
  static DLL_PUBLIC int last_imagedata_id;
  /* True of the data is owned by the structure.  */
  bool own;
};
#endif
