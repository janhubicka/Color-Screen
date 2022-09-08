#ifndef PATCHES_H
#define PATCHES_H
#include <vector>
#include "render.h"
#include "scr-detect.h"
#include "imagedata.h"

class patches
{
  public:
    typedef int patch_index_t;
    patches (image_data &, render &, color_class_map &map, int max_patch_size);
    ~patches ();
    struct patch
      {
	/* A pixel containing the patch.  */
	unsigned short x,y;
	/* Number of pixels of a given color.  */
	unsigned short pixels : 15;
	/* Overall number of pixels assigned to the patch.  */
	unsigned short overall_pixels : 15;
	/* Color of the patch (r=0, g=1, b=2).  */
	unsigned short color : 3;
	/* Sum of luminosities of pixels belonging to the patch.  */
	luminosity_t luminosity_sum;
      };
    struct patch &
    get_patch (patch_index_t index)
    {
      return m_vec[index - 1];
    }
    int
    get_patch_color (int x, int y)
    {
      return m_map[y * m_width + x] & 3;
    }
    patch_index_t
    get_patch_index (int x,int y)
    {
      return m_map[y * m_width + x] >> 2;
    }
    patch_index_t
    num_patches ()
    {
      return m_vec.size ();
    }
    /* Find coordinates of nearest patch in each color.  Set to -1 if patch is not found.  */
    bool
    nearest_patches (coord_t x, coord_t y, int *rx, int *ry, patch_index_t *rp);
    bool
    fast_nearest_patches (int x, int y, int *rx, int *ry, patch_index_t *rp);
private:
    int m_width, m_height;
    void
    set_patch_index (int x, int y, patch_index_t index, int color)
    {
      m_map[y * m_width + x] = (index << 2) + color;
#if 0
      if (get_patch_color (x,y) != color)
	abort ();
      if (get_patch_index (x,y) != index)
	abort ();
#endif
    }
    std::vector<patch> m_vec;
    patch_index_t *m_map;
    static const bool debug = false;
};
#endif
