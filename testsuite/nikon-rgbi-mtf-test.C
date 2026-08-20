#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../src/libcolorscreen/include/colorscreen.h"
#include "../src/libcolorscreen/include/imagedata.h"
#include "../src/libcolorscreen/include/render-parameters.h"

using namespace colorscreen;

/* Return the first downward 50-percent crossing of MEASUREMENT in MTF50.
   Linear interpolation keeps the regression insensitive to FFT padding.  */
static bool
measurement_mtf50 (const mtf_measurement &measurement, double *mtf50)
{
  if (!mtf50 || measurement.size () < 2)
    return false;
  for (size_t i = 1; i < measurement.size (); i++)
    {
      const double f0 = measurement.get_freq (i - 1);
      const double f1 = measurement.get_freq (i);
      const double c0 = measurement.get_contrast (i - 1);
      const double c1 = measurement.get_contrast (i);
      if (!std::isfinite (f0) || !std::isfinite (f1)
          || !std::isfinite (c0) || !std::isfinite (c1))
        return false;
      if (c0 >= 50 && c1 <= 50 && c0 != c1)
        {
          *mtf50 = f0 + (50 - c0) * (f1 - f0) / (c1 - c0);
          return std::isfinite (*mtf50) && *mtf50 > 0;
        }
    }
  return false;
}

struct scan_result
{
  std::array<double, 4> mtf50 = { 0, 0, 0, 0 };
};

/* Measure all four native scanner channels in FILENAME and store MTF50 in OUT.
   Return false and print a diagnostic when the real-data contract is broken.  */
static bool
measure_scan (const char *filename, scan_result *out)
{
  const char *top_srcdir = getenv ("top_srcdir");
  if (!top_srcdir || !*top_srcdir)
    top_srcdir = "..";
  const std::string path = std::string (top_srcdir) + "/testsuite/" + filename;

  image_data image;
  const char *error = nullptr;
  if (!image.load (path.c_str (), false, &error, nullptr))
    {
      fprintf (stderr, "Cannot load Nikon RGBI MTF fixture %s: %s\n",
               path.c_str (), error ? error : "unknown error");
      return false;
    }
  if (!image.has_rgb () || !image.has_grayscale_or_ir ())
    {
      fprintf (stderr, "Nikon RGBI fixture did not load all four channels: %s\n",
               filename);
      return false;
    }

  render_parameters parameters;
  parameters.gamma = 1.0;
  for (int channel = 0; channel < 4; channel++)
    {
      slanted_edge_parameters edge_parameters;
      edge_parameters.channel = channel;
      edge_parameters.same_capture = channel != 0;
      edge_parameters.name = std::string (filename) + " channel "
                             + std::to_string (channel);
      const slanted_edge_results edge
          = slanted_edge_mtf (parameters, image, image.get_area (),
                              edge_parameters, nullptr);
      if (!edge.success || !edge.measurement.size ())
        {
          fprintf (stderr,
                   "Nikon RGBI slanted edge failed for %s channel %d: %s\n",
                   filename, channel,
                   edge.error.empty () ? "no measurement produced"
                                       : edge.error.c_str ());
          return false;
        }

      /* The library API is intentionally side-effect free; callers decide
         whether to append a qualified measurement to render parameters.  */
      if (!parameters.sharpen.scanner_mtf.measurements.empty ())
        {
          fprintf (stderr, "Nikon RGBI measurement modified render parameters\n");
          return false;
        }
      if (edge.measurement.channel != channel
          || edge.measurement.same_capture != (channel != 0))
        {
          fprintf (stderr,
                   "Nikon RGBI measurement metadata mismatch for channel %d\n",
                   channel);
          return false;
        }
      if (edge.edge_angle < 3.5 || edge.edge_angle > 6.5)
        {
          fprintf (stderr,
                   "Unexpected Nikon razor angle %.6g for %s channel %d\n",
                   edge.edge_angle, filename, channel);
          return false;
        }
      if (!measurement_mtf50 (edge.measurement, &out->mtf50[channel]))
        {
          fprintf (stderr,
                   "Nikon RGBI measurement has no usable MTF50 for %s channel %d\n",
                   filename, channel);
          return false;
        }
    }
  return true;
}

/* Verify that native-channel MTF measurement distinguishes a sharp and a
   deliberately defocused real Nikon RGB+IR capture.  */
static bool
test_nikon_rgbi_mtf_focus ()
{
  scan_result sharp, defocused;
  if (!measure_scan ("nikon-razor-sharp-rgbi.tif", &sharp)
      || !measure_scan ("nikon-razor-defocus-rgbi.tif", &defocused))
    return false;

  for (int channel = 0; channel < 4; channel++)
    if (sharp.mtf50[channel] <= 2 * defocused.mtf50[channel])
      {
        fprintf (stderr,
                 "Nikon focus ordering failed for channel %d: sharp MTF50 %.6g, "
                 "defocused %.6g cycles/pixel\n",
                 channel, sharp.mtf50[channel], defocused.mtf50[channel]);
        return false;
      }

  /* If all four values are numerically identical, CHANNEL has almost certainly
     been treated as metadata while the same mixed image layer was measured.  */
  const auto [minimum, maximum]
      = std::minmax_element (sharp.mtf50.begin (), sharp.mtf50.end ());
  if (*maximum - *minimum < 1e-6)
    {
      fprintf (stderr,
               "Nikon native-channel MTF curves are unexpectedly identical\n");
      return false;
    }
  return true;
}

int
main ()
{
  puts ("1..1");
  if (test_nikon_rgbi_mtf_focus ())
    {
      puts ("ok 1 - Nikon RGB+IR native-channel focus MTF regression");
      return 0;
    }
  puts ("not ok 1 - Nikon RGB+IR native-channel focus MTF regression");
  return 1;
}
