#include <cstdio>

extern "C" void adobe_dng_reference_warp (
    int width, int height, double center_h, double center_v,
    const double kr[4], double dst_h, double dst_v,
    double *src_h, double *src_v);

struct fixture
{
    const char *name;
    int width, height;
    double cx, cy;
    double kr[4];
    double x, y;
};

int main ()
{
    const fixture f[] = {
        {"classic", 1000, 1000, 0.5, 0.5,
         {1.0, 0.05, -0.02, 0.005}, 600, 700},
        {"offcenter_tl", 1001, 701, 0.23, 0.67,
         {0.992, 0.045, -0.028, 0.009}, 0, 0},
        {"offcenter_tr", 1001, 701, 0.23, 0.67,
         {0.992, 0.045, -0.028, 0.009}, 1000, 0},
        {"offcenter_bl", 1001, 701, 0.23, 0.67,
         {0.992, 0.045, -0.028, 0.009}, 0, 700},
        {"offcenter_br", 1001, 701, 0.23, 0.67,
         {0.992, 0.045, -0.028, 0.009}, 1000, 700},
        {"offcenter_mid1", 1001, 701, 0.23, 0.67,
         {0.992, 0.045, -0.028, 0.009}, 123, 456},
        {"offcenter_mid2", 1001, 701, 0.23, 0.67,
         {0.992, 0.045, -0.028, 0.009}, 827, 51},
        {"offcenter_mid3", 1001, 701, 0.23, 0.67,
         {0.992, 0.045, -0.028, 0.009}, 230, 469},
        {"coolscan_tl", 4000, 3000, 0.560586, 0.482547,
         {0.99508, 0.0245411, -0.0521967, 0.0325757}, 0, 0},
        {"coolscan_br", 4000, 3000, 0.560586, 0.482547,
         {0.99508, 0.0245411, -0.0521967, 0.0325757}, 3999, 2999},
        {"coolscan_mid", 4000, 3000, 0.560586, 0.482547,
         {0.99508, 0.0245411, -0.0521967, 0.0325757}, 317, 2411},
        {"edge_ratio_below_one", 1000, 800, 0.41, 0.62,
         {0.9, 0.0, 0.0, 0.0}, 901, 87}
    };

    for (const fixture &q : f)
      {
        double sx, sy;
        adobe_dng_reference_warp (q.width, q.height, q.cx, q.cy,
                                   q.kr, q.x, q.y, &sx, &sy);
        std::printf ("%-22s %d %d %.17g %.17g "
                     "%.17g %.17g %.17g %.17g "
                     "%.17g %.17g %.17g %.17g\n",
                     q.name, q.width, q.height, q.cx, q.cy,
                     q.kr[0], q.kr[1], q.kr[2], q.kr[3],
                     q.x, q.y, sx, sy);
      }
}
