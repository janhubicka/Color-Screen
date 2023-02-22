#ifndef TIFFWRITER_H
#include <tiffio.h>
class tiff_writer
{
public:
  tiff_writer (const char *filename, int width, int height, int depth, bool alpha, const char **error);
  ~tiff_writer ();
  bool write_row ();
  uint16_t *row16bit ()
  {
    return (uint16_t *)outrow;
  }
  uint8_t *row8bit ()
  {
    return (uint8_t *)outrow;
  }

private:
  TIFF *out;
  void *outrow;
  int y;
};
#endif
