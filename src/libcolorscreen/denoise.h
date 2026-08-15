/* Tiled scalar denoising utilities.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#ifndef DENOISE_H
#define DENOISE_H
#include "include/base.h"
#include "include/color.h"
#include "include/progress-info.h"
#include "include/render-parameters.h"
#include <vector>
#include <omp.h>

namespace colorscreen
{

/* Reflect coordinate P into an image axis of length SIZE.  Repeat the edge
   sample on both sides.  Unlike a single clamp/reflection this also works when
   the filter border is wider than the image.  */
inline int
denoise_reflect_coordinate (int p, int size) noexcept
{
  if (size <= 1)
    return 0;
  const int64_t period = 2 * (int64_t)size;
  int64_t q = (int64_t)p % period;
  if (q < 0)
    q += period;
  if (q >= size)
    q = period - 1 - q;
  return (int)q;
}

/* Class managing denoising process.
   T is the data type (float or double).  */
template <typename T>
class denoising
{
public:
  /* Set up denoising for given parameters.  */
  denoising (const denoise_parameters &params, int max_threads,
             bool use_support = false);
  /* Destructor.  */
  ~denoising ();

  /* Size of tile processed without borders.  */
  int
  get_basic_tile_size () const noexcept
  {
    return m_tile_size - m_border_size * 2;
  }

  /* Size of tile processed with borders.  */
  int
  get_tile_size_with_borders () const noexcept
  {
    return m_tile_size;
  }

  /* Size of border.  */
  int
  get_border_size () const noexcept
  {
    return m_border_size;
  }

  /* Allocate memory for tile for given THREAD_ID.  */
  void init (int thread_id);

  /* Put pixel VAL at coordinates X, Y for given THREADID.  */
  void
  put_pixel (int threadid, int x, int y, T val)
  {
    m_data[threadid].tile[y * m_tile_size + x] = val;
  }

  /* Put collection support for pixel X,Y.  Support is an inverse-noise-like
     measure: one means roughly one full scanner-pixel contribution, values
     below one represent sub-pixel/weakly collected samples, and values above
     one represent multiple contributing samples.  It is only used when the
     object was constructed with USE_SUPPORT=true.  */
  void
  put_support (int threadid, int x, int y, T support)
  {
    assert (m_use_support);
    m_data[threadid].support[y * m_tile_size + x] = support;
  }

  /* Get filtered output pixel at coordinates X, Y for given THREADID.
     Call after process_tile().  */
  T
  get_pixel (int threadid, int x, int y) const
  {
    return m_data[threadid].out_tile[y * m_tile_size + x];
  }

  /* Apply denoising to the tile for given THREAD_ID.  */
  void process_tile (int thread_id, progress_info *progress);

private:
  /* Denoising parameters.  */
  denoise_parameters m_params;
  /* True if collection support participates in patch/range distances and
     candidate accumulation.  */
  bool m_use_support = false;
  /* Size of border required by the active filter kernel.  */
  int m_border_size = 0;
  /* Size of tile.  */
  int m_tile_size = 0;

  /* Data for a single tile.  */
  struct tile_data
  {
    /* Tile data.  */
    std::vector<T> tile;
    /* Output tile data.  */
    std::vector<T> out_tile;
    /* Collection support for confidence-aware pre-demosaic filtering.  */
    std::vector<T> support;
    /* Auxiliary buffers.  */
    std::vector<T> aux1;
    std::vector<T> aux2;
    std::vector<T> aux3;
    /* True if initialized.  */
    bool initialized = false;
  };
  /* Data for all threads.  */
  std::vector<tile_data> m_data;
};

/* Tiled RGB-vector denoising used after screen demosaicing.  One similarity
   weight is computed from all three color components and the same weight is
   applied to red, green and blue.  This avoids channel-dependent neighbor
   choices changing chromaticity as a side effect of denoising.  */
class rgb_denoising
{
public:
  rgb_denoising (const denoise_parameters &params, int max_threads);
  int get_basic_tile_size () const noexcept
  {
    return m_tile_size - m_border_size * 2;
  }
  int get_tile_size_with_borders () const noexcept { return m_tile_size; }
  int get_border_size () const noexcept { return m_border_size; }
  void init (int thread_id);
  void put_pixel (int thread_id, int x, int y, rgbdata val)
  {
    m_data[thread_id].tile[y * m_tile_size + x] = val;
  }
  rgbdata get_pixel (int thread_id, int x, int y) const
  {
    return m_data[thread_id].out_tile[y * m_tile_size + x];
  }
  void process_tile (int thread_id, progress_info *progress);

private:
  denoise_parameters m_params;
  int m_border_size = 0;
  int m_tile_size = 0;
  struct tile_data
  {
    std::vector<rgbdata> tile;
    std::vector<rgbdata> out_tile;
    std::vector<luminosity_t> integral;
    std::vector<luminosity_t> diff;
    std::vector<luminosity_t> total_weight;
    bool initialized = false;
  };
  std::vector<tile_data> m_data;
};

/* Denoising worker. Denoise DATA to OUT which both has dimensions
   WIDTH*HEIGHT. DATA are accessed using GETDATA function and PARAM can be used
   to pass extra data around. PARAMS specifies denoising parameters.
   PROGRESS is progress info. PARALLEL is true if parallel execution is
   requested. O is output type name, MEM_O is memory output type, T is data
   type name, P is extra bookkeeping parameter type. DT is a type to do
   denoising in.  */
template <typename O, typename mem_O, typename T, typename P,
          O (*getdata) (T data, int_point_t p, int width, P param), typename DT>
nodiscard_attr bool
denoise (mem_O *out, T data, P param, int width, int height,
         const denoise_parameters &params, progress_info *progress,
         bool parallel = true)
{
  if (params.get_mode () == denoise_parameters::none)
    return true;
  if (width < 0 || height < 0)
    return false;
  if (!width || !height)
    return true;

  /* Tile borders must always be read from the original image.  Callers are
     allowed to use the same storage for input and output, so take one
     immutable snapshot before any tile starts writing its result.  */
  std::vector<O> source ((size_t)width * height);
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      source[(size_t)y * width + x]
          = getdata (data, { x, y }, width, param);

  int nthreads = parallel ? omp_get_max_threads () : 1;
  denoising<DT> d (params, nthreads);

  int xtiles
      = (width + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
  int ytiles
      = (height + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();

  if (progress)
    progress->set_task ("denoising (non-local means)", xtiles * ytiles);

#pragma omp parallel for default(none) schedule(dynamic) collapse(2) shared(  \
        width, height, d, progress, out, parallel, source) if (parallel)
  for (int y = 0; y < height; y += d.get_basic_tile_size ())
    for (int x = 0; x < width; x += d.get_basic_tile_size ())
      {
        if (progress && progress->cancel_requested ())
          continue;
        int id = parallel ? omp_get_thread_num () : 0;
        d.init (id);
        for (int yy = 0; yy < d.get_tile_size_with_borders (); yy++)
          for (int xx = 0; xx < d.get_tile_size_with_borders (); xx++)
            {
              int px = x + xx - d.get_border_size ();
              int py = y + yy - d.get_border_size ();

              px = denoise_reflect_coordinate (px, width);
              py = denoise_reflect_coordinate (py, height);
              d.put_pixel (id, xx, yy, source[(size_t)py * width + px]);
            }
        d.process_tile (id, progress);
        for (int yy = 0; yy < d.get_basic_tile_size (); yy++)
          for (int xx = 0; xx < d.get_basic_tile_size (); xx++)
            if (y + yy < height && x + xx < width)
              out[(y + yy) * width + x + xx] = d.get_pixel (
                  id, xx + d.get_border_size (), yy + d.get_border_size ());
        if (progress)
          progress->inc_progress ();
      }
  if (progress && progress->cancelled ())
    return false;
  return true;
}

/* Denoising worker using functors.  */
template <typename DT, typename GETDATA, typename SETDATA>
nodiscard_attr bool
denoise (int width, int height, GETDATA getdata, SETDATA setdata,
         const denoise_parameters &params, progress_info *progress,
         bool parallel = true)
{
  if (params.get_mode () == denoise_parameters::none)
    return true;
  if (width < 0 || height < 0)
    return false;
  if (!width || !height)
    return true;

  /* SETDATA may update the same storage read by GETDATA.  Snapshot it so
     neighbouring tiles cannot observe one another's already denoised border.  */
  std::vector<DT> source ((size_t)width * height);
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      source[(size_t)y * width + x] = getdata (x, y);

  int nthreads = parallel ? omp_get_max_threads () : 1;
  denoising<DT> d (params, nthreads);

  int xtiles
      = (width + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
  int ytiles
      = (height + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();

  if (progress)
    progress->set_task ("denoising", xtiles * ytiles);

#pragma omp parallel for default(none) schedule(dynamic) collapse(2) shared(  \
        width, height, d, progress, setdata, parallel, source) if (parallel)
  for (int y = 0; y < height; y += d.get_basic_tile_size ())
    for (int x = 0; x < width; x += d.get_basic_tile_size ())
      {
        if (progress && progress->cancel_requested ())
          continue;
        int id = parallel ? omp_get_thread_num () : 0;
        d.init (id);
        for (int yy = 0; yy < d.get_tile_size_with_borders (); yy++)
          for (int xx = 0; xx < d.get_tile_size_with_borders (); xx++)
            {
              int px = x + xx - d.get_border_size ();
              int py = y + yy - d.get_border_size ();

              px = denoise_reflect_coordinate (px, width);
              py = denoise_reflect_coordinate (py, height);
              d.put_pixel (id, xx, yy, source[(size_t)py * width + px]);
            }
        d.process_tile (id, progress);
        for (int yy = 0; yy < d.get_basic_tile_size (); yy++)
          for (int xx = 0; xx < d.get_basic_tile_size (); xx++)
            if (y + yy < height && x + xx < width)
              setdata (x + xx, y + yy, d.get_pixel (
                  id, xx + d.get_border_size (), yy + d.get_border_size ()));
        if (progress)
          progress->inc_progress ();
      }
  if (progress && progress->cancelled ())
    return false;
  return true;
}

/* Confidence-aware scalar denoising for collected screen-element values.
   GETSUPPORT returns the raw analyze-* collection support for each sample.
   A support of one reproduces the ordinary filter exactly; sub-pixel values
   have less influence and samples with zero support may be reconstructed from
   reliable neighbours instead of acting as observations themselves.  */
template <typename DT, typename GETDATA, typename GETSUPPORT, typename SETDATA>
nodiscard_attr bool
denoise_with_support (int width, int height, GETDATA getdata,
                      GETSUPPORT getsupport, SETDATA setdata,
                      const denoise_parameters &params,
                      progress_info *progress, bool parallel = true)
{
  if (params.get_mode () == denoise_parameters::none)
    return true;
  if (width < 0 || height < 0)
    return false;
  if (!width || !height)
    return true;

  std::vector<DT> source ((size_t)width * height);
  std::vector<DT> support ((size_t)width * height);
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      {
        source[(size_t)y * width + x] = getdata (x, y);
        support[(size_t)y * width + x] = getsupport (x, y);
      }

  int nthreads = parallel ? omp_get_max_threads () : 1;
  denoising<DT> d (params, nthreads, true);

  if (progress)
    {
      int xtiles
          = (width + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
      int ytiles
          = (height + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
      progress->set_task ("denoising collected screen samples", xtiles * ytiles);
    }

#pragma omp parallel for default(none) schedule(dynamic) collapse(2) shared(  \
        width, height, d, progress, setdata, parallel, source, support) if (parallel)
  for (int y = 0; y < height; y += d.get_basic_tile_size ())
    for (int x = 0; x < width; x += d.get_basic_tile_size ())
      {
        if (progress && progress->cancel_requested ())
          continue;
        int id = parallel ? omp_get_thread_num () : 0;
        d.init (id);
        for (int yy = 0; yy < d.get_tile_size_with_borders (); yy++)
          for (int xx = 0; xx < d.get_tile_size_with_borders (); xx++)
            {
              int px = denoise_reflect_coordinate (
                  x + xx - d.get_border_size (), width);
              int py = denoise_reflect_coordinate (
                  y + yy - d.get_border_size (), height);
              const size_t idx = (size_t)py * width + px;
              d.put_pixel (id, xx, yy, source[idx]);
              d.put_support (id, xx, yy, support[idx]);
            }
        d.process_tile (id, progress);
        for (int yy = 0; yy < d.get_basic_tile_size (); yy++)
          for (int xx = 0; xx < d.get_basic_tile_size (); xx++)
            if (y + yy < height && x + xx < width)
              setdata (x + xx, y + yy,
                       d.get_pixel (id, xx + d.get_border_size (),
                                    yy + d.get_border_size ()));
        if (progress)
          progress->inc_progress ();
      }
  return !progress || !progress->cancelled ();
}

/* Denoising worker for RGB data.  */
template <typename O, typename mem_O, typename T, typename P,
          O (*getdata) (T data, int_point_t p, int width, P param), typename DT>
nodiscard_attr bool
denoise_rgb (mem_O *out, T data, P param, int width, int height,
             const denoise_parameters &params, progress_info *progress,
             bool parallel = true)
{
  if (params.get_mode () == denoise_parameters::none)
    return true;
  if (width < 0 || height < 0)
    return false;
  if (!width || !height)
    return true;

  std::vector<O> source ((size_t)width * height);
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      source[(size_t)y * width + x]
          = getdata (data, { x, y }, width, param);

  int nthreads = parallel ? omp_get_max_threads () : 1;
  denoising<DT> d (params, nthreads * 3);

  int xtiles
      = (width + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
  int ytiles
      = (height + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();

  if (progress)
    progress->set_task ("denoising rgb (non-local means)", xtiles * ytiles);

#pragma omp parallel for default(none) schedule(dynamic) collapse(2) shared(  \
        width, height, d, progress, out, parallel, source) if (parallel)
  for (int y = 0; y < height; y += d.get_basic_tile_size ())
    for (int x = 0; x < width; x += d.get_basic_tile_size ())
      {
        if (progress && progress->cancel_requested ())
          continue;
        int id = parallel ? omp_get_thread_num () : 0;
        d.init (3 * id);
        d.init (3 * id + 1);
        d.init (3 * id + 2);

        for (int yy = 0; yy < d.get_tile_size_with_borders (); yy++)
          for (int xx = 0; xx < d.get_tile_size_with_borders (); xx++)
            {
              int px = x + xx - d.get_border_size ();
              int py = y + yy - d.get_border_size ();

              px = denoise_reflect_coordinate (px, width);
              py = denoise_reflect_coordinate (py, height);

              O pixel = source[(size_t)py * width + px];
              d.put_pixel (3 * id, xx, yy, pixel.red);
              d.put_pixel (3 * id + 1, xx, yy, pixel.green);
              d.put_pixel (3 * id + 2, xx, yy, pixel.blue);
            }
        d.process_tile (3 * id, progress);
        d.process_tile (3 * id + 1, progress);
        d.process_tile (3 * id + 2, progress);

        for (int yy = 0; yy < d.get_basic_tile_size (); yy++)
          for (int xx = 0; xx < d.get_basic_tile_size (); xx++)
            if (y + yy < height && x + xx < width)
              {
                out[(y + yy) * width + x + xx].red = d.get_pixel (
                    3 * id, xx + d.get_border_size (), yy + d.get_border_size ());
                out[(y + yy) * width + x + xx].green = d.get_pixel (
                    3 * id + 1, xx + d.get_border_size (), yy + d.get_border_size ());
                out[(y + yy) * width + x + xx].blue = d.get_pixel (
                    3 * id + 2, xx + d.get_border_size (), yy + d.get_border_size ());
              }
        if (progress)
          progress->inc_progress ();
      }
  if (progress && progress->cancelled ())
    return false;
  return true;
}

/* Denoising worker for RGB data using functors.  */
template <typename DT, typename GETDATA, typename SETDATA>
nodiscard_attr bool
denoise_rgb (int width, int height, GETDATA getdata, SETDATA setdata,
             const denoise_parameters &params, progress_info *progress,
             bool parallel = true)
{
  if (params.get_mode () == denoise_parameters::none)
    return true;
  if (width < 0 || height < 0)
    return false;
  if (!width || !height)
    return true;

  std::vector<rgbdata> source ((size_t)width * height);
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      source[(size_t)y * width + x] = getdata (x, y);

  int nthreads = parallel ? omp_get_max_threads () : 1;
  denoising<DT> d (params, nthreads * 3);

  int xtiles
      = (width + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
  int ytiles
      = (height + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();

  if (progress)
    progress->set_task ("denoising rgb", xtiles * ytiles);

#pragma omp parallel for default(none) schedule(dynamic) collapse(2) shared(  \
        width, height, d, progress, setdata, parallel, source) if (parallel)
  for (int y = 0; y < height; y += d.get_basic_tile_size ())
    for (int x = 0; x < width; x += d.get_basic_tile_size ())
      {
        if (progress && progress->cancel_requested ())
          continue;
        int id = parallel ? omp_get_thread_num () : 0;
        d.init (3 * id);
        d.init (3 * id + 1);
        d.init (3 * id + 2);

        for (int yy = 0; yy < d.get_tile_size_with_borders (); yy++)
          for (int xx = 0; xx < d.get_tile_size_with_borders (); xx++)
            {
              int px = x + xx - d.get_border_size ();
              int py = y + yy - d.get_border_size ();

              px = denoise_reflect_coordinate (px, width);
              py = denoise_reflect_coordinate (py, height);

              rgbdata pixel = source[(size_t)py * width + px];
              d.put_pixel (3 * id, xx, yy, pixel.red);
              d.put_pixel (3 * id + 1, xx, yy, pixel.green);
              d.put_pixel (3 * id + 2, xx, yy, pixel.blue);
            }
        d.process_tile (3 * id, progress);
        d.process_tile (3 * id + 1, progress);
        d.process_tile (3 * id + 2, progress);

        for (int yy = 0; yy < d.get_basic_tile_size (); yy++)
          for (int xx = 0; xx < d.get_basic_tile_size (); xx++)
            if (y + yy < height && x + xx < width)
              {
                setdata (x + xx, y + yy, {
                    (luminosity_t)d.get_pixel (3 * id, xx + d.get_border_size (), yy + d.get_border_size ()),
                    (luminosity_t)d.get_pixel (3 * id + 1, xx + d.get_border_size (), yy + d.get_border_size ()),
                    (luminosity_t)d.get_pixel (3 * id + 2, xx + d.get_border_size (), yy + d.get_border_size ())
                });
              }
        if (progress)
          progress->inc_progress ();
      }
  if (progress && progress->cancelled ())
    return false;
  return true;
}

/* Denoise a complete RGB color field with common vector weights.  Range and
   patch distances use mean squared RGB distance, keeping STRENGTH and
   BILATERAL_SIGMA_R on approximately the same per-channel scale as the
   scalar filters.  */
template <typename GETDATA, typename SETDATA>
nodiscard_attr bool
denoise_rgb_vector (int width, int height, GETDATA getdata, SETDATA setdata,
                    const denoise_parameters &params, progress_info *progress,
                    bool parallel = true)
{
  if (params.get_mode () == denoise_parameters::none)
    return true;
  if (width < 0 || height < 0)
    return false;
  if (!width || !height)
    return true;

  std::vector<rgbdata> source ((size_t)width * height);
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      source[(size_t)y * width + x] = getdata (x, y);

  int nthreads = parallel ? omp_get_max_threads () : 1;
  rgb_denoising d (params, nthreads);
  int xtiles
      = (width + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
  int ytiles
      = (height + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
  if (progress)
    progress->set_task ("denoising demosaiced color", xtiles * ytiles);

#pragma omp parallel for default(none) schedule(dynamic) collapse(2) shared(  \
        width, height, d, progress, setdata, parallel, source) if (parallel)
  for (int y = 0; y < height; y += d.get_basic_tile_size ())
    for (int x = 0; x < width; x += d.get_basic_tile_size ())
      {
        if (progress && progress->cancel_requested ())
          continue;
        int id = parallel ? omp_get_thread_num () : 0;
        d.init (id);
        for (int yy = 0; yy < d.get_tile_size_with_borders (); yy++)
          for (int xx = 0; xx < d.get_tile_size_with_borders (); xx++)
            {
              int px = denoise_reflect_coordinate (
                  x + xx - d.get_border_size (), width);
              int py = denoise_reflect_coordinate (
                  y + yy - d.get_border_size (), height);
              d.put_pixel (id, xx, yy, source[(size_t)py * width + px]);
            }
        d.process_tile (id, progress);
        for (int yy = 0; yy < d.get_basic_tile_size (); yy++)
          for (int xx = 0; xx < d.get_basic_tile_size (); xx++)
            if (y + yy < height && x + xx < width)
              setdata (x + xx, y + yy,
                       d.get_pixel (id, xx + d.get_border_size (),
                                    yy + d.get_border_size ()));
        if (progress)
          progress->inc_progress ();
      }
  return !progress || !progress->cancelled ();
}

/* Confidence-aware version of pre-demosaic RGB screen-element denoising.
   The same collection support applies to all scanner-RGB components of one
   physical screen sample.  The components are still filtered independently;
   common/vector RGB similarity is intentionally deferred to the redesigned
   post-demosaic stage.  */
template <typename DT, typename GETDATA, typename GETSUPPORT, typename SETDATA>
nodiscard_attr bool
denoise_rgb_with_support (int width, int height, GETDATA getdata,
                          GETSUPPORT getsupport, SETDATA setdata,
                          const denoise_parameters &params,
                          progress_info *progress, bool parallel = true)
{
  if (params.get_mode () == denoise_parameters::none)
    return true;
  if (width < 0 || height < 0)
    return false;
  if (!width || !height)
    return true;

  std::vector<rgbdata> source ((size_t)width * height);
  std::vector<DT> support ((size_t)width * height);
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      {
        source[(size_t)y * width + x] = getdata (x, y);
        support[(size_t)y * width + x] = getsupport (x, y);
      }

  int nthreads = parallel ? omp_get_max_threads () : 1;
  denoising<DT> d (params, nthreads * 3, true);

  if (progress)
    {
      int xtiles
          = (width + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
      int ytiles
          = (height + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
      progress->set_task ("denoising collected RGB screen samples",
                          xtiles * ytiles);
    }

#pragma omp parallel for default(none) schedule(dynamic) collapse(2) shared(  \
        width, height, d, progress, setdata, parallel, source, support) if (parallel)
  for (int y = 0; y < height; y += d.get_basic_tile_size ())
    for (int x = 0; x < width; x += d.get_basic_tile_size ())
      {
        if (progress && progress->cancel_requested ())
          continue;
        int id = parallel ? omp_get_thread_num () : 0;
        d.init (3 * id);
        d.init (3 * id + 1);
        d.init (3 * id + 2);
        for (int yy = 0; yy < d.get_tile_size_with_borders (); yy++)
          for (int xx = 0; xx < d.get_tile_size_with_borders (); xx++)
            {
              int px = denoise_reflect_coordinate (
                  x + xx - d.get_border_size (), width);
              int py = denoise_reflect_coordinate (
                  y + yy - d.get_border_size (), height);
              const size_t idx = (size_t)py * width + px;
              rgbdata pixel = source[idx];
              for (int c = 0; c < 3; c++)
                {
                  int tid = 3 * id + c;
                  d.put_pixel (tid, xx, yy, pixel[c]);
                  d.put_support (tid, xx, yy, support[idx]);
                }
            }
        d.process_tile (3 * id, progress);
        d.process_tile (3 * id + 1, progress);
        d.process_tile (3 * id + 2, progress);
        for (int yy = 0; yy < d.get_basic_tile_size (); yy++)
          for (int xx = 0; xx < d.get_basic_tile_size (); xx++)
            if (y + yy < height && x + xx < width)
              setdata (
                  x + xx, y + yy,
                  { (luminosity_t)d.get_pixel (
                        3 * id, xx + d.get_border_size (),
                        yy + d.get_border_size ()),
                    (luminosity_t)d.get_pixel (
                        3 * id + 1, xx + d.get_border_size (),
                        yy + d.get_border_size ()),
                    (luminosity_t)d.get_pixel (
                        3 * id + 2, xx + d.get_border_size (),
                        yy + d.get_border_size ()) });
        if (progress)
          progress->inc_progress ();
      }
  return !progress || !progress->cancelled ();
}

/* Auto-select the type for denoising.  */
template <typename O, typename mem_O, typename T, typename P,
          O (*getdata) (T data, int_point_t p, int width, P param)>
nodiscard_attr bool
denoise (mem_O *out, T data, P param, int width, int height,
         const denoise_parameters &params, progress_info *progress,
         bool parallel = true)
{
  return denoise<O, mem_O, T, P, getdata, float>(out, data, param, width,
                                                  height, params, progress,
                                                  parallel);
}

template <typename O, typename mem_O, typename T, typename P,
          O (*getdata) (T data, int_point_t p, int width, P param)>
nodiscard_attr bool
denoise_rgb (mem_O *out, T data, P param, int width, int height,
             const denoise_parameters &params, progress_info *progress,
             bool parallel = true)
{
  return denoise_rgb<O, mem_O, T, P, getdata, float>(out, data, param, width,
                                                      height, params, progress,
                                                      parallel);
}

}
#endif
