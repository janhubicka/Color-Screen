#include <array>
#include "fftw3.h"
#include "icc.h"
#include "include/colorscreen.h"
#include "include/tiff-writer.h"
#include "include/histogram.h"
#include "render.h"
namespace colorscreen
{
namespace
{
constexpr const int tile_width=128;
constexpr const int tile_height=tile_width;
static constexpr const int fft_size = tile_width / 2 + 1;
typedef std::array<double, tile_width * tile_height> tile_t;
typedef std::array<fftw_complex, fft_size * tile_width> fft_2d;

bool
collect_tile (tile_t &tile, image_data &scan, const render &render, int x, int y, progress_info *progress)
{
  x -= tile_width / 2;
  y -= tile_height / 2;
  if (x + tile_width >= scan.width)
    x = scan.width - tile_width;
  if (y + tile_height >= scan.height)
    y = scan.height - tile_height;
  for (int yy = 0; yy < tile_width; yy++)
    for (int xx = 0; xx < tile_height; xx++)
      tile[yy * tile_width + xx] = render.fast_get_img_pixel (xx + x, yy + y);
  return true;
}

luminosity_t getval(fft_2d &fft_tile, int x, int y)
{
  return sqrt (fft_tile[y * fft_size + x][0] * fft_tile[y * fft_size + x][0] + fft_tile[y * fft_size + x][1] * fft_tile[y * fft_size + x][1]);
}


coord_t xperiod (int x)
{
  if (!x)
    return 0;
  if (x > tile_width / 2)
    x -= tile_width;
  return tile_width / 2.0 / x;
}

coord_t yperiod (int y)
{
  if (!y)
    return 0;
  if (y > tile_height / 2)
    y -= tile_height;
  return tile_height / 2.0 / y;
}



bool
save_fft_tile (fft_2d &fft_tile, const std::string &base, int x, int y, coord_t min_period, coord_t max_period, const char **error)
{
  char suf[30];
  sprintf (suf, "-%i-%i.tif", y, x);
  std::string name = base + suf;
  tiff_writer_params p;
  void *buffer;
  size_t len = create_linear_srgb_profile (&buffer);
  p.filename = name.c_str ();
  p.depth = 32;
  p.hdr = true;
  p.width = fft_size;
  p.height = tile_height;
  p.icc_profile = buffer;
  p.icc_profile_len = len;
  tiff_writer tiff (p, error);
  free (buffer);
  if (*error)
    return false;
  histogram hist, hsum_hist, vsum_hist;

  double vsum[fft_size];
  double hsum[tile_height];

  memset (hsum, 0, sizeof (hsum));
  memset (vsum, 0, sizeof (vsum));

  for (int y = 0; y < tile_height; y++)
    for (int x = 0; x < fft_size; x++)
      {
	vsum[x] += getval (fft_tile, x, y);
	hsum[y] += getval (fft_tile, x, y);
      }
  for (int y = 0; y < tile_height; y++)
    hsum_hist.pre_account (hsum [y]);
  for (int x = 0; x < fft_size; x++)
    vsum_hist.pre_account (vsum [x]);
  for (int y = 0; y < tile_height; y++)
    for (int x = 0; x < fft_size; x++)
      for (int i = 0; i < 2; i++)
	hist.pre_account (fabs (fft_tile[y * fft_size + x][i]));
  hist.finalize_range (65536);
  hsum_hist.finalize_range (65536);
  vsum_hist.finalize_range (65536);
  for (int y = 0; y < tile_height; y++)
    hsum_hist.account (hsum [y]);
  for (int x = 0; x < fft_size; x++)
    vsum_hist.account (vsum [x]);
  for (int y = 0; y < tile_height; y++)
    for (int x = 0; x < fft_size; x++)
      for (int i = 0; i < 2; i++)
	hist.account (fabs (fft_tile[y * fft_size + x][i]));
  hist.finalize ();
  hsum_hist.finalize ();
  vsum_hist.finalize ();
  luminosity_t lmax = hist.find_max (0.01);
  luminosity_t vmax = vsum_hist.find_max (0.01);
  luminosity_t hmax = hsum_hist.find_max (0.01);
  for (int y = 0; y < tile_height; y++)
    {
      for (int x = 0; x < fft_size; x++)
        {
          //float t = (min_period < yperiod (y) && max_period > yperiod (y)) * 0.5 + (min_period < xperiod (x) && max_period > xperiod (x)) * 0.5;
	  float t = hsum[y] / hmax + vsum[x] / vmax;
	  tiff.put_hdr_pixel (x, fabs (fft_tile[y * fft_size + x][0])/lmax, fabs (fft_tile[y * fft_size + x][1])/lmax, t);
	}
      if (!tiff.write_row ())
        {
	  *error = "error writting tiff file with FFT data";
	  return false;
        }
    }
  return true;
}
bool
save_tile (tile_t tile, const std::string &base, int x, int y, const char **error)
{
  char suf[30];
  sprintf (suf, "-%i-%i.tif", y, x);
  std::string name = base + suf;
  tiff_writer_params p;
  void *buffer;
  size_t len = create_linear_srgb_profile (&buffer);
  p.filename = name.c_str ();
  p.depth = 32;
  p.hdr = true;
  p.width = tile_width;
  p.height = tile_height;
  p.icc_profile = buffer;
  p.icc_profile_len = len;
  tiff_writer tiff (p, error);
  free (buffer);
  if (*error)
    return false;
  for (int y = 0; y < tile_height; y++)
    {
      for (int x = 0; x < tile_width; x++)
	tiff.put_hdr_pixel (x, tile[y * tile_width + x], tile[y * tile_width + x], tile[y * tile_width + x]);
      if (!tiff.write_row ())
        {
	  *error = "error writting tiff file with FFT data";
	  return false;
        }
    }
  return true;
}

struct summary
{
  double vsum[fft_size];
  double hsum[tile_height];
};

void
summarize (fft_2d &fft_tile, struct summary *sum)
{
  memset (sum->hsum, 0, sizeof (sum->hsum));
  memset (sum->vsum, 0, sizeof (sum->vsum));
  for (int y = 0; y < tile_height; y++)
    for (int x = 0; x < fft_size; x++)
      {
	sum->vsum[x] += getval (fft_tile, x, y);
	sum->hsum[y] += getval (fft_tile, x, y);
      }
}

}
bool has_regular_screen (image_data &scan, const has_regular_screen_params &params, progress_info *progress, const char **error)
{
  if (scan.width < tile_width || scan.height < tile_height)
    {
      *error = "input scan is too small";
      return false;
    }

  tile_t tile;
  fft_2d fft_tile;
  fftw_plan plan_2d;
  render_parameters rparams;
  std::vector <summary> sum (params.ntilesy * params.ntilesx);

  *error = NULL;
  //rparams.gamma = scan.gamma;
  render render (scan, rparams, 256);
  render.precompute_all (true, false, (rgbdata){1.0/3, 1.0/3, 1.0/3}, progress);
  plan_2d = fftw_plan_dft_r2c_2d (tile_width, tile_height, tile.data (), fft_tile.data (), FFTW_ESTIMATE);
  for (int y = 0; y < params.ntilesy; y++)
    for (int x = 0; x < params.ntilesx; x++)
      {
	collect_tile (tile, scan, render, scan.width * (x + 0.5) / params.ntilesx, scan.height * (y + 0.5) / params.ntilesy, progress);
        fftw_execute (plan_2d);
	if (params.save_tiles && !save_tile (tile, params.tile_base, x, y, error))
	  return false;
	if (params.save_fft && !save_fft_tile (fft_tile, params.fft_base, x, y, params.min_period, params.max_period, error))
	  return false;
	summarize (fft_tile, &sum[y * params.ntilesx + x]);
      }
  int r = 3;
  bool found = false;
  for (int x = r; x < fft_size - 2 * r; x++)
    {
      coord_t period = xperiod (x);
      int nok = 0;
      if (period < params.min_period || period > params.max_period)
	continue;
      if (params.verbose)
	printf ("Period %.2f: ", period);
      for (int t = 0; t < params.ntilesx * params.ntilesy;t++)
        {
	  luminosity_t vs1 = 0,vs2 = 0,vs3 = 0;
	  for (int i = x - r; i < x; i++)
	    vs1 += sum[t].vsum[i];
	  for (int i = x; i < x + r; i++)
	    vs2 += sum[t].vsum[i];
	  for (int i = x + r; i < x + 2 * r; i++)
	    vs3 += sum[t].vsum[i];
	  if (params.verbose)
	    printf ("%.2f  ", vs2 / std::max (vs1, vs3));
	  if (vs2 / std::max (vs1, vs3) > params.threshold)
	    nok++;
        }
      if (params.verbose)
        printf ("ok: %i", nok);
      if (nok > params.ntilesx * params.ntilesy * params.tiles_treshold)
	{
	  if (params.verbose)
	    printf ("found regular screen");
	  found = true;
	}
      if (params.verbose)
        printf ("\n", nok);
    }
  return found;
}
}
