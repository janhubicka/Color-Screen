#ifndef IMAGEDATA_H
#define IMAGEDATA_H
#include "dllpublic.h"
#include "color.h"
#include "progress-info.h"

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

  image_data ();
  ~image_data ();
  /* Dimensions of image data.  */
  int width, height;
  /* Maximal value of the image data.  */
  int maxval;
  /* Unique id of the image (used for caching).  */
  unsigned long id;

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
  bool load (const char *name, const char **error, progress_info *progress = NULL);
private:
  image_data_loader *loader;
  /* True of the data is owned by the structure.  */
  bool own;
};
#endif
