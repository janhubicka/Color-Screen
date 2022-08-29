#ifndef RENDER_SCR_DETECT_H
#define RENDER_SCR_DETECT_H
#include "render.h"
#include "scr-detect.h"
class render_scr_detect : public render
{
public:
  render_scr_detect (scr_detect_parameters &param, image_data &img, render_parameters &rparam, int dstmaxval)
    : render (img, rparam, dstmaxval)
  {
    m_scr_detect.set_parameters (param);
  }
  scr_detect::color_class classify_pixel (int x, int y)
  {
    if (x < 0 || x >= m_img.width || y < 0 || y > m_img.height)
      return scr_detect::unknown;
    return m_scr_detect.classify_color (m_img.rgbdata[y][x].r, m_img.rgbdata[y][x].g, m_img.rgbdata[y][x].b);
  }
  enum render_scr_detect_type_t
  {
    render_type_original,
    render_type_pixel_colors,
  };
  DLL_PUBLIC static void render_tile (enum render_scr_detect_type_t render_type, scr_detect_parameters &param, image_data &img, render_parameters &rparam,
				      bool color, unsigned char *pixels, int rowstride, int pixelbytes, int width, int height,
				      double xoffset, double yoffset, double step);
protected:
  scr_detect m_scr_detect;
};
#endif
