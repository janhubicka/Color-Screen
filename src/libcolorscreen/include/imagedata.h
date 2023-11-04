#ifndef IMAGEDATA_H
#define IMAGEDATA_H
#include "dllpublic.h"
#include "color.h"
#include "progress-info.h"

class image_data_loader;
class stitch_project;

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
  void *icc_profile;

  DLL_PUBLIC image_data ();
  DLL_PUBLIC ~image_data ();
  /* Dimensions of image data.  */
  int width, height;
  /* Maximal value of the image data.  */
  int maxval;
  uint32_t icc_profile_size;
  /* Unique id of the image (used for caching).  */
  unsigned long id;
  float xdpi, ydpi;
  stitch_project *stitch;

  /* Begining of the viewport of stitched object.  */
  int xmin, ymin;

  /* Initialize loader for NAME.  Return true on success.
     If false is returned ERROR is initialized to error
     message.  */
  DLL_PUBLIC bool init_loader (const char *name, const char **error, progress_info *progress = NULL);
  /* True if grayscale allocation is needed
     (used after init_loader and before load_part).  */
  DLL_PUBLIC bool allocate_grayscale ();
  /* True if rgballocation is needed
     (used after init_loader and before load_part).  */
  DLL_PUBLIC bool allocate_rgb ();
  /* Load part of image. Initialize PERMILLE to status.
     If PERMILLE==1000 loading is finished.
     If false is returned ERROR is initialized.  */
  DLL_PUBLIC bool load_part (int *permille, const char **error);

  /* Allocate memory.  */
  DLL_PUBLIC bool allocate ();
  /* Load image data from file with auto-detection.  */
  DLL_PUBLIC bool load (const char *name, const char **error, progress_info *progress = NULL);
private:
  image_data_loader *loader;
  /* True of the data is owned by the structure.  */
  bool own;
};
#endif
