#include "dng_simple_image.h"

extern "C" void
adobe_dng_reference_warp (int width, int height,
                          double center_h, double center_v,
                          const double kr[4],
                          double dst_h, double dst_v,
                          double *src_h, double *src_v)
{
    dng_host host;
    AutoPtr<dng_negative> negative (dng_negative::Make (host));
    negative->SetMonochrome ();

    dng_simple_image srcImage (dng_rect (height, width), 1, ttFloat,
                               host.Allocator ());
    dng_simple_image dstImage (dng_rect (height, width), 1, ttFloat,
                               host.Allocator ());

    dng_warp_params_radial radial;
    dng_vector rv (4);
    rv [0] = kr [0];
    rv [1] = kr [1];
    rv [2] = kr [2];
    rv [3] = kr [3];
    radial.SetWarpRectilinear_1_3 (0, rv);

    dng_vector tan [1];
    tan [0] = dng_vector (2);
    tan [0] [0] = 0.0;
    tan [0] [1] = 0.0;

    AutoPtr<dng_warp_params> params (
        new dng_warp_params_rectilinear (
            1, radial, tan, dng_point_real64 (center_v, center_h)));

    dng_filter_warp filter (srcImage, dstImage, *negative, params);
    dng_point_real64 out = filter.GetSrcPixelPosition (
        dng_point_real64 (dst_v, dst_h), 0);
    *src_h = out.h;
    *src_v = out.v;
}
