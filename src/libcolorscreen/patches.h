/* Screen patches detection and manipulation.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#ifndef PATCHES_H
#define PATCHES_H

#include <vector>
#include <memory>
#include "include/progress-info.h"
#include "include/imagedata.h"
#include "scr-detect.h"
#include "render.h"

namespace colorscreen
{
/* Class representing a set of screen patches detected in an image.  */
class patches
{
  public:
    /* Index of a patch.  */
    typedef int patch_index_t;

    /* Initialize patches for given image IMG using RENDER and COLOR_MAP.
       MAX_PATCH_SIZE is the maximum size of a patch to be considered.
       PROGRESS is used to report progress and check for cancellation.  */
    patches (const image_data &img, render &render, color_class_map &color_map,
	     int max_patch_size, progress_info *progress);
    /* Free allocated memory.  */
    ~patches ();

    /* Descriptor of a single patch.  */
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

    /* Return reference to patch with given INDEX.  */
    struct patch &
    get_patch (patch_index_t index)
    {
      return m_vec[index - 1];
    }
    /* Return reference to patch with given INDEX.  */
    const struct patch &
    get_patch (patch_index_t index) const
    {
      return m_vec[index - 1];
    }

    /* Return color class of pixel at position P.  */
    pure_attr int
    get_patch_color (int_point_t p) const noexcept
    {
      return m_map[p.y * m_width + p.x] & 3;
    }

    /* Return index of patch at position P.  */
    pure_attr patch_index_t
    get_patch_index (int_point_t p) const noexcept
    {
      return m_map[p.y * m_width + p.x] >> 2;
    }

    /* Return number of detected patches.  */
    const_attr patch_index_t
    num_patches () const noexcept
    {
      return m_vec.size ();
    }

    /* Find coordinates of nearest patch in each color for position P.
       Set RP to patch indices and RX, RY to coordinates.
       Return true if patches were found.  */
    bool nearest_patches (point_t p, int *rx, int *ry, patch_index_t *rp) const;

    /* Fast version of nearest_patches for integer position P.  */
    bool fast_nearest_patches (int_point_t p, int *rx, int *ry, patch_index_t *rp) const;

private:
    /* Dimensions of the image.  */
    int m_width = 0, m_height = 0;

    /* Set patch index for pixel at position P.  */
    void
    set_patch_index (int_point_t p, patch_index_t index, int color)
    {
      m_map[p.y * m_width + p.x] = (index << 2) + color;
    }

    /* Vector of all detected patches.  */
    std::vector<patch> m_vec;
    /* Map of patch indices for each pixel.  */
    std::unique_ptr<patch_index_t[]> m_map;
    /* Enable debugging output.  */
    static const bool debug = false;
};
}
#endif
