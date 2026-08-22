/* Geometry detection logic for regular screens.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */
#include "analyze-paget.h"
#include "bitmap.h"
#include "include/base.h"
#include "include/colorscreen.h"
#include "include/screen-map.h"
#include "render-scr-detect.h"
#include "render-to-scr.h"
#include "solver.h"
#include <chrono>
#include <limits.h>
#include <memory>
namespace colorscreen {
extern void prune_render_scr_detect_caches();
namespace {
// const bool verbose = true;
const bool verbose = false;
const int verbose_confirm = 0;

/* Report-only counters and timings for one regular-screen detection run.
   REPORT_FILE controls whether clocks are sampled and whether the summary is
   written.  Normal detection without a report only updates a few counters.  */
struct detection_stats {
  using clock = std::chrono::steady_clock;
  using time_point = clock::time_point;

  FILE *report_file;
  const char *result = "no-initial-grid";
  const char *last_flood_failure = "none";
  scr_type type = Random;
  int regions = 0;
  long long seed_pixels = 0;
  int initial_grids = 0;
  int initial_solver_failures = 0;
  int flood_attempts = 0;
  int flood_failures = 0;
  int color_opt_failures = 0;
  int precompute_failures = 0;
  int classmap_builds = 0;
  int rgb_precomputes = 0;
  int patches = 0;
  bool legacy_preclassification_sharpening = false;
  double optimize_colors_ms = 0;
  double precompute_ms = 0;
  double classmap_ms = 0;
  double initial_solver_ms = 0;
  double flood_ms = 0;
  double final_solver_ms = 0;
  double mesh_solver_ms = 0;
  time_point total_start;

  /* Start statistics collection for REPORT_FILE.  */
  explicit detection_stats(FILE *report_file)
      : report_file(report_file),
        total_start(report_file ? clock::now() : time_point()) {}

  /* Return a stage start time when reporting is enabled.  */
  time_point start_timer() const {
    return report_file ? clock::now() : time_point();
  }

  /* Add time since START to ACCUMULATOR when reporting is enabled.  */
  void add_time(double *accumulator, time_point start) const {
    if (report_file)
      *accumulator +=
          std::chrono::duration<double, std::milli>(clock::now() - start)
              .count();
  }

  /* Write stable counters and stage timings to REPORT_FILE.  */
  ~detection_stats() {
    if (!report_file)
      return;
    double total_ms =
        std::chrono::duration<double, std::milli>(clock::now() - total_start)
            .count();
    const char *type_name =
        type >= Random && type < max_scr_type ? scr_names[(int)type].name
                                              : "none";
    fprintf(report_file,
            "detect_stats: result=%s type=%s regions=%i seed_pixels=%lld "
            "initial_grids=%i initial_solver_failures=%i flood_attempts=%i "
            "flood_failures=%i patches=%i last_flood_failure=%s "
            "color_opt_failures=%i precompute_failures=%i classmap_builds=%i "
            "rgb_precomputes=%i legacy_preclassification_sharpening=%i\n",
            result, type_name, regions, seed_pixels, initial_grids,
            initial_solver_failures, flood_attempts, flood_failures, patches,
            last_flood_failure, color_opt_failures, precompute_failures,
            classmap_builds, rgb_precomputes,
            legacy_preclassification_sharpening);
    fprintf(report_file,
            "detect_stats_ms: optimize_colors=%.3f precompute=%.3f "
            "classmap=%.3f initial_solver=%.3f flood=%.3f "
            "final_solver=%.3f mesh_solver=%.3f total=%.3f\n",
            optimize_colors_ms, precompute_ms, classmap_ms, initial_solver_ms,
            flood_ms, final_solver_ms, mesh_solver_ms, total_ms);
  }
};
/* Find the eight-connected patch of class C containing X, Y in COLOR_MAP.
   Store at most MAX_PATCH_SIZE pixels in ENTRIES and return the number stored.
   VISITED marks pixels belonging to patches already considered.  If PERMANENT
   is false, clear the bits set by this search before returning.  A return value
   equal to MAX_PATCH_SIZE means that the component reached the size limit and
   may be larger.  */
int find_patch(const color_class_map &color_map, scr_detect::color_class c,
               int x, int y, int max_patch_size, int_point_t *entries,
               bitmap_2d *visited, bool permanent) {
  if (x < 0 || y < 0 || x >= color_map.width || y >= color_map.height)
    return 0;
  scr_detect::color_class t = color_map.get_class(x, y);
  if (t != c)
    return 0;
  if (visited->set_bit(x, y))
    return 0;
  int start = 0, end = 1;
  entries[0].x = x;
  entries[0].y = y;

  while (start < end) {
    int cx = entries[start].x;
    int cy = entries[start].y;
    for (int yy = std::max(cy - 1, 0); yy < std::min(cy + 2, color_map.height);
         yy++)
      for (int xx = std::max(cx - 1, 0); xx < std::min(cx + 2, color_map.width);
           xx++)
        if ((xx != cx || yy != cy) && color_map.get_class(xx, yy) == t) {
          if (visited->set_bit(xx, yy))
            continue;
          entries[end].x = xx;
          entries[end].y = yy;
          end++;
          if (end == max_patch_size)
            goto done;
        }
    start++;
  }
done:
  if (!permanent)
    for (int i = 0; i < end; i++)
      visited->clear_bit(entries[i].x, entries[i].y);
  return end;
}

/* Compute the arithmetic mean of the SIZE pixels in ENTRIES and store it in X
   and Y.  Return false for an empty patch or when the image pixel nearest the
   mean is not part of the patch.  */

bool patch_center(int_point_t *entries, int size, coord_t *x, coord_t *y) {
  if (size <= 0)
    return false;

  int xsum = 0;
  int ysum = 0;
  for (int i = 0; i < size; i++) {
    xsum += entries[i].x;
    ysum += entries[i].y;
  }
  *x = (2 * xsum + size) / (coord_t)(2 * size);
  *y = (2 * ysum + size) / (coord_t)(2 * size);
  /* Confirm that the center is inside of the patch.  */
  for (int i = 0; i < size; i++)
    if ((int)(*x + (coord_t)0.5) == entries[i].x &&
        (int)(*y + (coord_t)0.5) == entries[i].y)
      return true;
  return false;
}

/* Return true if COLOR_MAP contains a class-C component of at least
   MIN_PATCH_SIZE pixels at X, Y.  Store the successful match priority in
   PRIORITY.  VISITED supplies temporary component-search state; strip pixels
   are not retained there because a strip is one connected component.  */

bool confirm_strip(const color_class_map *color_map, coord_t x, coord_t y,
                   scr_detect::color_class c, int min_patch_size, int *priority,
                   bitmap_2d *visited) {
  int_point_t entries[min_patch_size + 1];
  /* Since strips are not isolated do not mark them as visited so we do not
   * block walk from other spot.  */
  int size = find_patch(*color_map, c, (int)(x + (coord_t)0.5),
                        (int)(y + (coord_t)0.5), min_patch_size + 1, entries,
                        visited, false);
  if (size < min_patch_size)
    return false;
  *priority = 7;
  return true;
}

const int npatches = 5;

/* Image-space center of one classified screen patch.  */
struct patch_info {
  coord_t x, y;
};

/* Try to identify an initial Dufay-like grid starting at X, Y in COLOR_MAP.
   TYPE selects the Dufay, Dioptichrome, Improved Dioptichrome, or Omnicolore
   color permutation and lattice angle.  On success, store the observed patch
   centers in SPARAM.  VISITED prevents repeatedly starting from the same
   connected component and REPORT_FILE receives optional diagnostics.

   This function validates a NPATCHES by 2 * NPATCHES grid.  That is strong
   evidence for the screen type, but it is intentionally only an initial
   estimate; flood_fill() later collects enough patches for the final geometry.

   The Dufay arrangement is

   GBGBGB
   RRRRRR
   GBGBGB
   RRRRRR

   Dioptichrome B exchanges red and green:

   RBRBRB
   GGGGGG
   RBRBRB
   GGGGGG

   Improved Dioptichrome B and Omnicolore exchange red and blue:

   GRGRGR
   BBBBBB
   GRGRGR
   BBBBBB

   Improved Dioptichrome B has an angle of approximately 107.77 degrees
   between lattice vectors; the other supported Dufay-like screens are
   approximately orthogonal.  MY_RED, MY_GREEN, and MY_BLUE implement these
   color permutations below.  */
bool try_guess_screen(FILE *report_file, scr_type type,
                      const color_class_map &color_map,
                      solver_parameters &sparam, int x, int y,
                      bitmap_2d *visited) {
  const int max_size = 1000;
  struct patch_info rbpatches[npatches][npatches * 2];
  int_point_t entries[max_size];
  const char *scrname = scr_names[(int)type].name;

  scr_detect::color_class my_red = scr_detect::red;
  scr_detect::color_class my_green = scr_detect::green;
  scr_detect::color_class my_blue = scr_detect::blue;
  const char *cnames[3] = {"red", "green", "blue"};

  if (type == DioptichromeB)
    std::swap(my_red, my_green);
  else if (type == ImprovedDioptichromeB || type == Omnicolore)
    std::swap(my_red, my_blue);

  /* First try to find a green patch.  */
  int size =
      find_patch(color_map, my_green, x, y, max_size, entries, visited, true);
  if (size == 0 || size == max_size)
    return false;
  if (!patch_center(entries, size, &rbpatches[0][0].x, &rbpatches[0][0].y))
    return false;
  if (report_file && verbose)
    fprintf(report_file,
            "%s: Trying to start search at %i %i with initial green patch of "
            "size %i and center %f %f\n",
            scrname, x, y, size, rbpatches[0][0].x, rbpatches[0][0].y);

  bool patch_found = false;
  int adjacent_size = 0;

  /* Now find adjacent blue patch.  */
  for (int i = 0; i < size && !patch_found; i++) {
    int x = entries[i].x;
    for (y = entries[i].y + 1; y < color_map.height && !patch_found; y++) {
      scr_detect::color_class t = color_map.get_class(x, y);
      if (t == my_blue) {
        /* Do not mark as visited so we can revisit.  */
        adjacent_size = find_patch(color_map, my_blue, x, y, max_size, entries,
                                   visited, false);
        patch_found = patch_center(entries, adjacent_size,
                                   &rbpatches[0][1].x,
                                   &rbpatches[0][1].y);
      } else if (t != scr_detect::unknown)
        break;
    }
  }

  if (!patch_found) {
    if (report_file && verbose)
      fprintf(report_file, "%s: %s patch not found\n", scrname,
              cnames[(int)my_blue]);
    return false;
  }

  /* Now find row of alternating green and blue patches;  keep updating screen
     geometry (patch_stepx, patch_stepy).  */
  coord_t patch_stepx = rbpatches[0][1].x - rbpatches[0][0].x;
  coord_t patch_stepy = rbpatches[0][1].y - rbpatches[0][0].y;
  if (report_file && verbose)
    fprintf(report_file,
            "%s: found %s patch of size %i and center %f %f guessing patch "
            "distance %f %f\n",
            scrname, cnames[(int)my_blue], adjacent_size,
            rbpatches[0][1].x, rbpatches[0][1].y, patch_stepx, patch_stepy);
  for (int p = 2; p < npatches * 2; p++) {
    int nx = rbpatches[0][p - 1].x + patch_stepx;
    int ny = rbpatches[0][p - 1].y + patch_stepy;
    size = find_patch(color_map, (p & 1) ? my_blue : my_green, nx, ny, max_size,
                      entries, visited, false);
    if (size == 0 || size == max_size) {
      if (report_file && verbose)
        fprintf(report_file,
                "%s: Failed to guess %s patch 0, %i with steps %f %f\n",
                scrname, cnames[(int)my_blue], p, patch_stepx, patch_stepy);
      return false;
    }
    if (!patch_center(entries, size, &rbpatches[0][p].x, &rbpatches[0][p].y)) {
      if (report_file && verbose)
        fprintf(report_file, "%s: Center of patch 0, %i is not inside\n",
                scrname, p);
      return false;
    }
    patch_stepx = (rbpatches[0][p].x - rbpatches[0][0].x) / p;
    patch_stepy = (rbpatches[0][p].y - rbpatches[0][0].y) / p;
  }
  if (report_file && verbose)
    fprintf(report_file,
            "%s: Confirmed %i patches in alternating direction with "
            "distances %f %f\n",
            scrname, npatches * 2, patch_stepx, patch_stepy);

  /* Once the first row is known, extend it along the second lattice vector.  */
  for (int r = 1; r < npatches; r++) {
    coord_t vpatch_stepx;
    coord_t vpatch_stepy;

    if (type == ImprovedDioptichromeB) {
      coord_t angle = (coord_t)107.77 * (coord_t)M_PI / (coord_t)180;
      coord_t s = my_sin(angle);
      coord_t c = my_cos(angle);
      matrix2x2<coord_t> rotate(c, -s, s, c);
      rotate.apply_to_vector(patch_stepx, patch_stepy, &vpatch_stepx,
                             &vpatch_stepy);
      // vpatch_stepx *= 0.8;
      // vpatch_stepy *= 0.8;
    } else {
      vpatch_stepx = -patch_stepy;
      vpatch_stepy = patch_stepx;
    }

    int rx = rbpatches[r - 1][0].x + vpatch_stepx;
    int ry = rbpatches[r - 1][0].y + vpatch_stepy;
    int nx = rbpatches[r - 1][0].x + 2 * vpatch_stepx;
    int ny = rbpatches[r - 1][0].y + 2 * vpatch_stepy;
    int priority;
    if (!confirm_strip(&color_map, rx, ry, my_red, 1, &priority, visited)) {
      if (report_file && verbose)
        fprintf(report_file,
                "%s: Failed to confirm %s strip on way to %i,%i with "
                "steps %f %f (rotated %f %f)\n",
                scrname, cnames[(int)my_red], r, 0, patch_stepx, patch_stepy,
                vpatch_stepx, vpatch_stepy);
      return false;
    }
    size = find_patch(color_map, my_green, nx, ny, max_size, entries, visited,
                      false);
    if (size == 0 || size == max_size) {
      if (report_file && verbose)
        fprintf(report_file,
                "%s: Failed to guess patch %i,%i with steps %f %f "
                "(rotated %f %f)\n",
                scrname, r, 0, patch_stepx, patch_stepy, vpatch_stepx,
                vpatch_stepy);
      return false;
    }
    if (!patch_center(entries, size, &rbpatches[r][0].x, &rbpatches[r][0].y)) {
      if (report_file && verbose)
        fprintf(report_file, "%s: Center of patch %i,%i is not inside\n",
                scrname, r, 0);
      return false;
    }
    for (int p = 1; p < npatches * 2; p++) {
      int nx = rbpatches[r][p - 1].x + patch_stepx;
      int ny = rbpatches[r][p - 1].y + patch_stepy;
      size = find_patch(color_map, (p & 1) ? my_blue : my_green, nx, ny,
                        max_size, entries, visited, false);
      if (size == 0 || size == max_size) {
        if (report_file && verbose)
          fprintf(report_file,
                  "%s: Failed to guess patch %i,%i with steps %f %f\n", scrname,
                  r, p, patch_stepx, patch_stepy);
        return false;
      }
      if (!patch_center(entries, size, &rbpatches[r][p].x,
                        &rbpatches[r][p].y)) {
        if (report_file && verbose)
          fprintf(report_file, "%s: Center of patch %i,%i is not inside\n",
                  scrname, r, p);
        return false;
      }
    }
  }

  /* Be sure that every point is unique.  */
  for (int r = 0; r < npatches; r++)
    for (int p = 0; p < npatches * 2; p++)
      for (int r1 = 0; r1 <= r; r1++)
        for (int p1 = 0; p1 < (r1 == r ? p : npatches * 2); p1++)
          if (rbpatches[r][p].x == rbpatches[r1][p1].x &&
              rbpatches[r][p].y == rbpatches[r1][p1].y)
            return false;

  /* Add points to solver; this is mostly useful for debugging.  */
  sparam.remove_points();
  for (int r = 0; r < npatches; r++)
    for (int p = 0; p < npatches * 2; p++)
      sparam.add_point(
          {rbpatches[r][p].x, rbpatches[r][p].y},
          {p / (coord_t)2.0, (coord_t)r},
          (p & 1) ? (colorscreen::solver_parameters::point_color)my_blue
                  : (colorscreen::solver_parameters::point_color)my_green);
  return true;
}

/* Try to identify an initial Paget/Finlay grid starting at X, Y in
   COLOR_MAP.  On success, store observed patch centers in SPARAM.  VISITED
   prevents repeatedly starting from the same component and REPORT_FILE
   receives optional diagnostics.

   In image-oriented coordinates the screen is

     G   R   G
   B   B   B
     R   G   R
   B   B   B

   Detection uses diagonal coordinates, rotating the logical grid by 45
   degrees:

   G B G B
   B R B R
   G B G B  */
bool try_guess_paget_screen(FILE *report_file, const color_class_map &color_map,
                            solver_parameters &sparam, int x, int y,
                            bitmap_2d *visited) {
  const int max_size = 1000;
  struct patch_info rpatches[npatches][npatches];
  struct patch_info gpatches[npatches][npatches];
  struct patch_info bpatches[npatches * 2][npatches];
  int_point_t entries[max_size];

  /* Find initial green patch.  */
  int size = find_patch(color_map, scr_detect::green, x, y, max_size, entries,
                        visited, true);
  if (size == 0 || size == max_size)
    return false;
  if (!patch_center(entries, size, &gpatches[0][0].x, &gpatches[0][0].y))
    return false;
  if (report_file && verbose)
    fprintf(report_file,
            "Paget: Trying to start search at %i %i with initial green patch "
            "of size %i and center %f %f\n",
            x, y, size, gpatches[0][0].x, gpatches[0][0].y);

  bool patch_found = false;

  /* Find adjacent blue patch below the green one.
     G
       B

     */
  for (int i = 0; i < size && !patch_found; i++) {
    int x = entries[i].x;
    for (y = entries[i].y + 1; y < color_map.height && !patch_found; y++) {
      scr_detect::color_class t = color_map.get_class(x, y);
      if (t == scr_detect::blue) {
        /* Do not mark as visited so we can revisit.  */
        int size = find_patch(color_map, scr_detect::blue, x, y, max_size,
                              entries, visited, false);
        patch_found =
            patch_center(entries, size, &bpatches[0][0].x, &bpatches[0][0].y);
      } else if (t != scr_detect::unknown)
        break;
    }
  }
  if (!patch_found) {
    if (report_file && verbose)
      fprintf(report_file, "Paget: Blue patch not found\n");
    return false;
  }

  /* Below green patch should be two blue patches.

        G
      B   B

     Guess coordinates of second one and try to find it.  */
  coord_t b1patch_stepx = bpatches[0][0].x - gpatches[0][0].x;
  coord_t b1patch_stepy = bpatches[0][0].y - gpatches[0][0].y;
  coord_t b2patch_stepx = b1patch_stepy;
  coord_t b2patch_stepy = -b1patch_stepx;
  coord_t bx, by;
  size = find_patch(
      color_map, scr_detect::blue, gpatches[0][0].x + b2patch_stepx,
      gpatches[0][0].y + b2patch_stepy, max_size, entries, visited, false);
  if (!size || size == max_size) {
    if (report_file && verbose)
      fprintf(report_file,
              "Paget: Second blue patch not found with step %f %f\n",
              b2patch_stepx, b2patch_stepy);
    return false;
  }
  patch_found = patch_center(entries, size, &bx, &by);
  if (!patch_found || (bx == bpatches[0][0].x && by == bpatches[0][0].y)) {
    if (report_file && verbose)
      fprintf(report_file, "Paget: Center of second Blue patch not found\n");
    return false;
  }

  /* Order the blue patches so the first lattice step points right.  */
  if (b1patch_stepx < 0) {
    std::swap(b1patch_stepx, b2patch_stepx);
    std::swap(b1patch_stepy, b2patch_stepy);
    std::swap(bpatches[0][0].x, bx);
    std::swap(bpatches[0][0].y, by);
  }

  /* Now determine the distance between two green patches

       G
     B   B
           G

     And try to confirm second green patch.  */
  coord_t gpatch_stepx = (bpatches[0][0].x - gpatches[0][0].x) * 2;
  coord_t gpatch_stepy = (bpatches[0][0].y - gpatches[0][0].y) * 2;

  size = find_patch(
      color_map, scr_detect::green, gpatches[0][0].x + gpatch_stepx,
      gpatches[0][0].y + gpatch_stepy, max_size, entries, visited, false);
  if (!size || size == max_size) {
    if (report_file && verbose)
      fprintf(report_file, "Paget: Second green patch not found\n");
    return false;
  }
  patch_found =
      patch_center(entries, size, &gpatches[0][1].x, &gpatches[0][1].y);
  if (!patch_found) {
    if (report_file && verbose)
      fprintf(report_file, "Paget: Center of second green patch not found\n");
    return false;
  }

  /* See if we can predict third blue patch
       G
     B   B
           G
             B  */
  coord_t patch_stepx = (gpatches[0][1].x - gpatches[0][0].x) / 2;
  coord_t patch_stepy = (gpatches[0][1].y - gpatches[0][0].y) / 2;
  coord_t opatch_stepx = patch_stepy;
  coord_t opatch_stepy = -patch_stepx;
  size = find_patch(color_map, scr_detect::blue, gpatches[0][1].x + patch_stepx,
                    gpatches[0][1].y + patch_stepy, max_size, entries, visited,
                    false);
  if (!size || size == max_size) {
    if (report_file && verbose)
      fprintf(report_file, "Paget: Third blue patch not found\n");
    return false;
  }
  patch_found =
      patch_center(entries, size, &bpatches[0][1].x, &bpatches[0][1].y);
  if (!patch_found) {
    if (report_file && verbose)
      fprintf(report_file, "Paget: Center of third blue patch not found\n");
    return false;
  }

  if (report_file && verbose)
    fprintf(report_file,
            "Paget: found green patch of size %i and center %f %f guessing "
            "patch distance %f %f\n",
            size, gpatches[0][1].x, gpatches[0][1].y, patch_stepx, patch_stepy);
  for (int p = 2; p < npatches; p++) {
    int nx = bpatches[0][p - 1].x + patch_stepx;
    int ny = bpatches[0][p - 1].y + patch_stepy;
    size = find_patch(color_map, scr_detect::green, nx, ny, max_size, entries,
                      visited, false);
    if (size == 0 || size == max_size) {
      if (report_file && verbose)
        fprintf(report_file,
                "Paget: Failed to guess green patch 0, %i with steps %f %f\n",
                p, patch_stepx, patch_stepy);
      return false;
    }
    if (!patch_center(entries, size, &gpatches[0][p].x, &gpatches[0][p].y)) {
      if (report_file && verbose)
        fprintf(report_file, "Paget: Center of patch 0, %i is not inside\n", p);
      return false;
    }

    nx = gpatches[0][p].x + patch_stepx;
    ny = gpatches[0][p].y + patch_stepy;
    size = find_patch(color_map, scr_detect::blue, nx, ny, max_size, entries,
                      visited, false);
    if (size == 0 || size == max_size) {
      if (report_file && verbose)
        fprintf(report_file,
                "Paget: Failed to guess blue patch 0, %i with steps %f %f\n", p,
                patch_stepx, patch_stepy);
      return false;
    }
    if (!patch_center(entries, size, &bpatches[0][p].x, &bpatches[0][p].y)) {
      if (report_file && verbose)
        fprintf(report_file, "Paget: Center of patch 0, %i is not inside\n", p);
      return false;
    }

    patch_stepx = (gpatches[0][p].x - gpatches[0][0].x) / p / 2;
    patch_stepy = (gpatches[0][p].y - gpatches[0][0].y) / p / 2;
  }
  for (int p = 0; p < npatches; p++) {
    for (int q = 1; q < npatches * 2; q++)
      if (q & 1) {
        int nx = gpatches[(q - 1) / 2][p].x + opatch_stepx;
        int ny = gpatches[(q - 1) / 2][p].y + opatch_stepy;
        size = find_patch(color_map, scr_detect::blue, nx, ny, max_size,
                          entries, visited, false);
        if (size == 0 || size == max_size) {
          if (report_file && verbose)
            fprintf(report_file,
                    "Paget: Failed to guess blue patch %i, %i with "
                    "steps %f %f\n",
                    q, p, opatch_stepx, opatch_stepy);
          return false;
        }
        if (!patch_center(entries, size, &bpatches[q][p].x,
                          &bpatches[q][p].y)) {
          if (report_file && verbose)
            fprintf(report_file,
                    "Paget: Center of patch %i, %i is not inside\n", q, p);
          return false;
        }
        nx = bpatches[q - 1][p].x + opatch_stepx;
        ny = bpatches[q - 1][p].y + opatch_stepy;
        size = find_patch(color_map, scr_detect::red, nx, ny, max_size, entries,
                          visited, false);
        if (size == 0 || size == max_size) {
          if (report_file && verbose)
            fprintf(report_file,
                    "Paget: Failed to guess red patch %i, %i with "
                    "steps %f %f\n",
                    q, p, opatch_stepx, opatch_stepy);
          return false;
        }
        if (!patch_center(entries, size, &rpatches[q / 2][p].x,
                          &rpatches[q / 2][p].y)) {
          if (report_file && verbose)
            fprintf(report_file,
                    "Paget: Center of patch %i, %i is not inside\n", q, p);
          return false;
        }
      } else {
        int nx = bpatches[q - 1][p].x + opatch_stepx;
        int ny = bpatches[q - 1][p].y + opatch_stepy;
        size = find_patch(color_map, scr_detect::green, nx, ny, max_size,
                          entries, visited, false);
        if (size == 0 || size == max_size) {
          if (report_file && verbose)
            fprintf(report_file,
                    "Paget: Failed to guess green patch %i, %i with "
                    "steps %f %f\n",
                    q, p, opatch_stepx, opatch_stepy);
          return false;
        }
        if (!patch_center(entries, size, &gpatches[q / 2][p].x,
                          &gpatches[q / 2][p].y)) {
          if (report_file && verbose)
            fprintf(report_file,
                    "Paget: Center of patch %i, %i is not inside\n", q, p);
          return false;
        }
        nx = rpatches[(q - 1) / 2][p].x + opatch_stepx;
        ny = rpatches[(q - 1) / 2][p].y + opatch_stepy;
        size = find_patch(color_map, scr_detect::blue, nx, ny, max_size,
                          entries, visited, false);
        if (size == 0 || size == max_size) {
          if (report_file && verbose)
            fprintf(report_file,
                    "Paget: Failed to guess blue patch %i, %i with "
                    "steps %f %f\n",
                    q, p, opatch_stepx, opatch_stepy);
          return false;
        }
        if (!patch_center(entries, size, &bpatches[q][p].x,
                          &bpatches[q][p].y)) {
          if (report_file && verbose)
            fprintf(report_file,
                    "Paget: Center of patch %i, %i is not inside\n", q, p);
          return false;
        }
      }
  }

  sparam.remove_points();
  for (int r = 0; r < npatches; r++)
    for (int p = 0; p < npatches; p++) {
      sparam.add_point({gpatches[r][p].x, gpatches[r][p].y},
                       {(r + p) / 2.0f, (p - r) / 2.0f},
                       solver_parameters::green);
      sparam.add_point({bpatches[r * 2][p].x, bpatches[r * 2][p].y},
                       {(r + p + 0.5f) / 2.0f, (p - r + 0.5f) / 2.0f},
                       solver_parameters::blue);
    }
  if (verbose)
    printf("Paget: Initial screen found\n");

  return true;
}

/* Verify a classified patch of color C near X, Y in COLOR_MAP.  Accept only
   components whose size lies in MIN_PATCH_SIZE through MAX_PATCH_SIZE and whose
   center is within MAX_DISTANCE of the prediction.  Store the accepted center
   in CX, CY and a distance-derived priority in PRIORITY.  REPORT_FILE receives
   optional diagnostics and VISITED prevents a component from being accepted
   more than once.  */
bool confirm_patch(FILE *report_file, const color_class_map *color_map,
                   coord_t x, coord_t y, scr_detect::color_class c,
                   int min_patch_size, int max_patch_size, coord_t max_distance,
                   coord_t *cx, coord_t *cy, int *priority,
                   bitmap_2d *visited) {
  *cx = x;
  *cy = y;
  int_point_t entries[max_patch_size + 1];
  const char *fail = NULL;
  int size = find_patch(*color_map, c, (int)(x + (coord_t)0.5),
                        (int)(y + (coord_t)0.5), max_patch_size + 1, entries,
                        visited, true);
  if (size < min_patch_size) {
    if (!size)
      fail = "rejected: zero size";
    else
      fail = "rejected: too small";
  } else if (size > max_patch_size)
    fail = "rejected: too large";
  else if (!patch_center(entries, size, cx, cy))
    fail = "rejected: center not in patch";
  else if ((*cx - x) * (*cx - x) + (*cy - y) * (*cy - y) >
           max_distance * max_distance)
    fail = "rejected: distance out of tolerance";
  if (report_file && fail && verbose)
    fprintf(
        report_file,
        "size: %i (expecting %i...%i) coord: %f %f center %f %f color %i %s\n",
        size, min_patch_size, max_patch_size, x, y, *cx, *cy, (int)c,
        fail ? fail : "");
  // printf ("center %f %f\n", *cx, *cy);
  if (fail) {
    for (int i = 0; i < size; i++)
      visited->clear_bit(entries[i].x, entries[i].y);
    // printf ("%s %i %i color %i %i size %i %i...%i\n",
    // fail,(int)(x+0.5),(int)(y+0.5), (int)c, (int)color_map->get_class
    // ((int)(x+0.5),(int)(y+0.5)), size, min_patch_size, max_patch_size);
    return false;
  }
  coord_t dist = (*cx - x) * (*cx - x) + (*cy - y) * (*cy - y);
  if (dist < max_distance * max_distance / 128)
    *priority = 7;
  else if (dist < max_distance * max_distance / 32)
    *priority = 6;
  else if (dist < max_distance * max_distance / 8)
    *priority = 5;
  else
    *priority = 4;
  return true;
}

#define N_PRIORITIES 8

/* Confirm a predicted screen element from image data supplied by RENDER.
   COORDINATE1 and COORDINATE2 are the local lattice vectors and X, Y is the
   predicted image position.  T is the expected color class; WIDTH and HEIGHT
   bound interpolation.  Search within a fraction of MAX_DISTANCE, store the
   best position in RCX, RCY, and assign PRIORITY from contrast and displacement.
   SUM_RANGE controls the centroid-search footprint, PATCH_XSCALE and
   PATCH_YSCALE control the inner/outer sampling pattern, STRIP selects strip
   geometry, CORNERS excludes corner samples for diamond-shaped patches, and
   MIN_CONTRAST is the required inner-to-outer color ratio.  */
bool confirm(const render_scr_detect *render, point_t coordinate1,
             point_t coordinate2, coord_t x, coord_t y,
             scr_detect::color_class t, int width, int height,
             coord_t max_distance, coord_t *rcx, coord_t *rcy, int *priority,
             coord_t sum_range, coord_t patch_xscale, coord_t patch_yscale,
             bool strip, bool corners, luminosity_t min_contrast) {
  /* BESTCX and BESTCY are adjusted locations of the patch.  */
  coord_t bestcx = x, bestcy = y;
  /* BESTOUTER_LR, BESTOUTER_UD, and BESTOUTER_CORNERS are the sampled
     color fractions around the patch boundary.  BESTINNER is the fraction
     inside the patch.  */
  luminosity_t minsum = 0, bestinner = 0, bestouter_lr = 0,
               bestouter_corners = 0, bestouter_ud = 0;

  /* Analyze a sparse (2 * SAMPLE_STEPS + 1) square.  The inner
     (2 * SAMPLE_STEPS - 1) square should lie in the patch and the boundary
     should lie outside it.  OUTER_SPACE leaves a gap for moderately blurred
     patch boundaries.  */
  const int sample_steps = 2;
  const int outer_space = 1;
  // TODO: Works for Dufay.  */
  // const coord_t pixel_step = 0.1;

  /* Search just part of max distance range.  */
  const int max_distance_scale = 16;
  bool found = false;
  /* We go to both directions from given X and Y coordinates.  */
  sum_range = (coord_t)0.5 * sum_range;

  int xmin = my_ceil(std::min(
      std::min(coordinate1.x * sum_range, coordinate2.x * sum_range),
      std::min(-coordinate1.x * sum_range, -coordinate2.x * sum_range)));
  int xmax = my_ceil(std::max(
      std::max(coordinate1.x * sum_range, coordinate2.x * sum_range),
      std::max(-coordinate1.x * sum_range, -coordinate2.x * sum_range)));
  int ymin = my_ceil(std::min(
      std::min(coordinate1.y * sum_range, coordinate2.y * sum_range),
      std::min(-coordinate1.y * sum_range, -coordinate2.y * sum_range)));
  int ymax = my_ceil(std::max(
      std::max(coordinate1.y * sum_range, coordinate2.y * sum_range),
      std::max(-coordinate1.y * sum_range, -coordinate2.y * sum_range)));
  coord_t scaled_max_distance = max_distance / max_distance_scale;
  coord_t pixel_step = scaled_max_distance / 5;

  /* We want to sum symmetrically in each direction.  */
  ymin = xmin = std::min(std::min(std::min(xmin, ymin), -xmax), -ymax);
  ymax = xmax = /*std::min (xmax, ymax)*/ -ymin;

  if (verbose_confirm > 1)
    printf("pixel step: %f ranges %i %i distance %f\n", pixel_step, xmax - xmin,
           ymax - ymin, scaled_max_distance);

  /* Do not search near an image edge, where interpolation would use pixels
     outside the image.  A four-pixel border is required.  */
  if (y - scaled_max_distance + ymin - 4 < 0 ||
      y + scaled_max_distance + ymax + 4 >= height ||
      x - scaled_max_distance + xmin - 4 < 0 ||
      x + scaled_max_distance + xmax + 3 >= width)
    return false;

  if (!strip) {
    luminosity_t min = INT_MAX, max = INT_MIN;
    for (coord_t cy = std::max(y - scaled_max_distance, (coord_t)-ymin);
         cy <= std::min(y + scaled_max_distance, (coord_t)height - ymax);
         cy += pixel_step)
      for (coord_t cx = std::max(x - scaled_max_distance, (coord_t)-xmin);
           cx <= std::min(x + scaled_max_distance, (coord_t)width - xmax);
           cx += pixel_step) {
        int xstart = my_floor(cx + xmin);
        int ystart = my_floor(cy + ymin);
        coord_t xsum = 0;
        coord_t ysum = 0;
#define account(xx, yy, wx, wy)                                                \
  {                                                                            \
    rgbdata color = render->fast_precomputed_get_normalized_pixel({xx, yy});   \
    luminosity_t c = color[t];                                                 \
    xsum += c * wx * wy * (xx + (coord_t)0.5 - cx);                            \
    ysum += c * wx * wy * (yy + (coord_t)0.5 - cy);                            \
    max = std::max(max, c);                                                    \
    min = std::min(min,                                                        \
                   c); /*printf("  %7.2f %7.2f", (coord_t)wx, (coord_t)wy)*/   \
    ;                                                                          \
  }

        // luminosity_t c = color[t];
        // luminosity_t d = std::max (color[0] + color[1] + color[2],
        // (luminosity_t)0.0001); c = c / d;
        for (int yy = ystart; yy < ystart + ymax - ymin + 1; yy++) {
          coord_t wy = 1;
          if (yy == ystart)
            wy = 1 - (cy + ymin - ystart);
          if (yy == ystart + ymax - ymin)
            wy = (cy + ymin - ystart);
          coord_t wx = 1 - (cx + ymin - xstart);
          int xx = xstart;
          account(xx, yy, wx, wy);
          for (xx = xstart + 1; xx < xstart + xmax - ymin; xx++)
            account(xx, yy, 1, wy);
          account(xx, yy, (1 - wx), wy);
          // printf ("\n");
        }
#undef account

        coord_t sum = xsum * xsum + ysum * ysum;
        if (verbose_confirm > 1)
          printf(" trying %f %f : %f %f xsum %f ysum %f sum %f %s\n  ", cx, cy,
                 cx - x, cy - y, xsum, ysum, sum,
                 minsum > sum ? "new best" : "");
        if (!found || minsum > sum) {
          bestcx = cx;
          bestcy = cy;
          minsum = sum;
          found = true;
        }
      }
    if (verbose_confirm > 1) {
      int xstart = my_floor(bestcx + xmin);
      int ystart = my_floor(bestcy + ymin);
      printf("best %f %f : %f %f min %f max %f\n  ", bestcx, bestcy, bestcx - x,
             bestcy - y, min, max);
      for (int xx = xstart; xx < my_floor(bestcx); xx++)
        printf(" ");
      printf("|\n");

      for (int yy = ystart; yy < ystart + ymax - ymin + 1; yy++) {
        printf(yy == my_floor(bestcy) ? "->" : "  ");

#define account(xx, yy, wx, wy)                                                \
  {                                                                            \
    rgbdata color = render->fast_precomputed_get_normalized_pixel({xx, yy});   \
    luminosity_t c = color[t];                                                 \
    luminosity_t d =                                                           \
        std::max(color[0] + color[1] + color[2], (luminosity_t)0.0001f);       \
    c = c / d;                                                                 \
    putc(".oO*"[(int)((c - min) * (coord_t)3.9999 / (max - min))], stdout);    \
  }
        // printf (" %7.4f", c); }
        // printf (" %i %i %5.2f*%5.2f*%5.2f", xx, yy, c, (coord_t)wx,
        // wy); }
        int xx = xstart;
        account(xx, yy, wx, wy);
        for (xx = xstart + 1; xx < xstart + xmax - ymin; xx++)
          account(xx, yy, 1, wy);
        account(xx, yy, (1 - wx), wy);
#undef account
        printf("\n");
      }
    }
#if 0
      //printf ("%i %i %i %i\n",xmin,xmax,ymin,ymax);
      for (coord_t cy = std::max (y - scaled_max_distance, (coord_t)-ymin); cy <= std::min (y + scaled_max_distance, (coord_t)height - ymax); cy+= pixel_step)
	for (coord_t cx = std::max (x - scaled_max_distance, (coord_t)-xmin); cx <= std::min (x + scaled_max_distance, (coord_t)width - xmax); cx+= pixel_step)
	  {
	    coord_t xsum = 0;
	    coord_t ysum = 0;
	    int maxdistsq = xmax * xmax + ymax * ymax;
	    switch ((int) t)
	      {
		case 0:
		for (int yy = floor (cy + ymin) ; yy < ceil (cy + ymax); yy++)
		  for (int xx = floor (cx + xmin) ; xx < ceil (cx + xmax); xx++)
		    {
		      rgbdata color = render->fast_precomputed_get_adjusted_pixel ({xx, yy});
		      //xsum += std::max (color[t], (luminosity_t)0) * (xx + 0.5 - cx);
		      //ysum += std::max (color[t], (luminosity_t)0) * (yy + 0.5 - cy);
		      xsum += color[t] * (xx + 0.5 - cx) * (maxdistsq - (yy - cy) * (yy - cy) - (xx - cx) * (xx - cx) + 1);
		      ysum += color[t] * (yy + 0.5 - cy) * (maxdistsq - (yy - cy) * (yy - cy) - (xx - cx) * (xx - cx) + 1);
		    }
		break;
		case 1:
		for (int yy = floor (cy + ymin) ; yy < ceil (cy + ymax); yy++)
		  for (int xx = floor (cx + xmin) ; xx < ceil (cx + xmax); xx++)
		    {
		      rgbdata color = render->fast_precomputed_get_adjusted_pixel ({xx, yy});
		      //xsum += std::max (color[t], (luminosity_t)0) * (xx + 0.5 - cx);
		      //ysum += std::max (color[t], (luminosity_t)0) * (yy + 0.5 - cy);
		      xsum += color[t] * (xx + 0.5 - cx) * (maxdistsq - (yy - cy) * (yy - cy) - (xx - cx) * (xx - cx) + 1);
		      ysum += color[t] * (yy + 0.5 - cy) * (maxdistsq - (yy - cy) * (yy - cy) - (xx - cx) * (xx - cx) + 1);
		    }
		break;
		case 2:
		for (int yy = floor (cy + ymin) ; yy < ceil (cy + ymax); yy++)
		  for (int xx = floor (cx + xmin) ; xx < ceil (cx + xmax); xx++)
		    {
		      rgbdata color = render->fast_precomputed_get_adjusted_pixel ({xx, yy});
		      //xsum += std::max (color[t], (luminosity_t)0) * (xx + 0.5 - cx);
		      //ysum += std::max (color[t], (luminosity_t)0) * (yy + 0.5 - cy);
		      xsum += color[t] * (xx + 0.5 - cx) * (maxdistsq - (yy - cy) * (yy - cy) - (xx - cx) * (xx - cx) + 1);
		      ysum += color[t] * (yy + 0.5 - cy) * (maxdistsq - (yy - cy) * (yy - cy) - (xx - cx) * (xx - cx) + 1);
		    }
		break;
		default:
		abort ();
	      }
	   coord_t sum = xsum * xsum + ysum * ysum;
	   //printf ("%f %f: %f %f %f\n", cx-x, cy-y, xsum, ysum, sum);
	   if (!found || minsum > sum)
	     {
	       bestcx = cx;
	       bestcy = cy;
	       minsum = sum;
	       found = true;
	   }
	 }
      if (!found)
	return false;
#endif
  } else {
    bestcx = x;
    bestcy = y;
  }
  // int nouter = 0, ninner = 0;
  luminosity_t min = 0;
  if (verbose_confirm > 1) {
    printf("coordinate1 %f %f\n", coordinate1.x, coordinate1.y);
    printf("coordinate2 %f %f\n", coordinate2.x, coordinate2.y);
    printf("patch_xscale %f %f t %i strip %i corner %i\n", patch_xscale,
           patch_yscale, t, strip, corners);
  }
  for (int yy = -sample_steps - outer_space; yy <= sample_steps + outer_space;
       yy++) {
    /* Leave a gap between inner and outer samples for blurred boundaries.  */
    if (yy == -sample_steps - outer_space + 1 || yy == sample_steps)
      yy += outer_space;
    for (int xx = -sample_steps - outer_space; xx <= sample_steps + outer_space;
         xx++) {
      if (xx == -sample_steps - outer_space + 1 || xx == sample_steps)
        xx += outer_space;
      coord_t ax =
          bestcx +
          (xx *
           (1 / ((coord_t)sample_steps + 2 * outer_space) * patch_xscale)) *
              coordinate1.x +
          (yy *
           (1 / ((coord_t)sample_steps + 2 * outer_space) * patch_yscale)) *
              coordinate2.x;
      coord_t ay =
          bestcy +
          (xx *
           (1 / ((coord_t)sample_steps + 2 * outer_space) * patch_xscale)) *
              coordinate1.y +
          (yy *
           (1 / ((coord_t)sample_steps + 2 * outer_space) * patch_yscale)) *
              coordinate2.y;

      rgbdata d = render->get_adjusted_pixel({ax, ay});
      luminosity_t color[3] = {d.red, d.green, d.blue};

      luminosity_t sum = color[0] + color[1] + color[2];
      color[0] = std::max(color[0], (luminosity_t)0);
      color[1] = std::max(color[1], (luminosity_t)0);
      color[2] = std::max(color[2], (luminosity_t)0);
      sum = std::max(sum, (luminosity_t)0.0001);
      // sum=1;
      luminosity_t val = color[t] / sum;
      min = std::min(val, min);
      if (verbose_confirm > 2)
        printf(" [% 6.2F % 6.2F]:", ax - bestcx, ay - bestcy);
      if (verbose_confirm > 1)
        printf("   r% 8.3F g% 8.3F b% 8.3F *% 8.3F*", color[0] * 100,
               color[1] * 100, color[2] * 100, val);
      if (/*sum > 0 && color[t] > 0*/ 1) {
        bool lr = (xx == -sample_steps - outer_space ||
                   xx == sample_steps + outer_space);
        bool ud = (yy == -sample_steps - outer_space ||
                   yy == sample_steps + outer_space);
        if (lr && ud)
          bestouter_corners += val;
        else if (lr)
          bestouter_lr += val;
        else if (ud)
          bestouter_ud += val;
        else
          bestinner += val;
#if 0
	      if (corners && ((xx == -sample_steps - outer_space || xx == sample_steps + outer_space) && (yy == -sample_steps - outer_space || yy == sample_steps + outer_space)))
		{
		  if (verbose_confirm > 1)
		    printf ("X");
		}
	      else if ((!strip && (xx == -sample_steps - outer_space || xx == sample_steps + outer_space)) || yy == -sample_steps - outer_space || yy == sample_steps + outer_space)
		{
		  if (xx == -sample_steps - outer_space || xx == sample_steps + outer_space)
		    bestouter_lr += val;
		  if (yy == -sample_steps - outer_space || yy == sample_steps + outer_space)
		    bestouter_ud += val;
		  if (verbose_confirm > 1)
		    printf ("O");
		}
	      else
		{
		  bestinner += val;// ninner++;
		  if (verbose_confirm > 1)
		    printf ("I");
		}
#endif
      }
    }
    if (verbose_confirm > 1)
      printf("\n");
  }
  // printf ("%f %f %f\n",min, bestinner, bestouter);

  *rcx = bestcx;
  *rcy = bestcy;
  /*  For sample_steps == 2:
      O O O O O
      O I I I O
      O I I I O
      O I I I O
      O O O O O  */
  if (!strip && !corners) {
    int ninner = (2 * sample_steps - 1) * (2 * sample_steps - 1);
    /* We count left/right and up/down separately. Corners are counted twice.
     */
    int nouter = (2 * sample_steps - 1) * 2;
    bestinner -= min * ninner;
    bestouter_lr -= min * nouter;
    bestouter_ud -= min * nouter;
    bestouter_corners -= min * 4;
    bestinner *= (1 / (luminosity_t)ninner);
    bestouter_lr *= (1 / (luminosity_t)nouter);
    bestouter_ud *= (1 / (luminosity_t)nouter);
    bestouter_corners *= (1 / (luminosity_t)4);
  }
  /*  For sample_steps == 2:
        O O O
      O I I I O
      O I I I O
      O I I I O
        O O O    */
  else if (!strip) {
    int ninner = (2 * sample_steps - 1) * (2 * sample_steps - 1);
    int nouter = (2 * sample_steps - 1) * 2;

    bestinner -= min * ninner;
    bestouter_lr -= min * nouter;
    bestouter_ud -= min * nouter;
    bestinner *= (1 / (luminosity_t)ninner);
    bestouter_lr *= (1 / (luminosity_t)nouter);
    bestouter_ud *= (1 / (luminosity_t)nouter);
    bestouter_corners = 0;
  }
  /*  For sample_steps == 2:
      O O O O O
      I I I I I
      I I I I I
      I I I I I
      O O O O O  */
  else {
    int ninner = (2 * sample_steps + 1) * (2 * sample_steps - 1);
    int nouter = (2 * sample_steps + 1) * 2;
    bestinner += bestouter_lr;
    bestouter_ud += bestouter_corners;
    bestouter_lr = 0;
    bestouter_corners = 0;

    bestinner -= min * ninner;
    bestouter_ud -= min * nouter;
    bestinner *= (1 / (luminosity_t)ninner);
    bestouter_ud *= (1 / (luminosity_t)nouter);
  }
  luminosity_t bestouter = std::max(
      std::max(std::max(bestouter_ud, (luminosity_t)0.00001), bestouter_lr),
      bestouter_corners);
  coord_t dist = (bestcx - x) * (bestcx - x) + (bestcy - y) * (bestcy - y);
  if (bestinner <= 0 || bestinner < bestouter_lr * min_contrast) {
    if (verbose_confirm)
      printf("FAILED: given:%f %f best:%f %f inner:%f outer:%f %f ratio: "
             "%f color:%i min:%f\n",
             x, y, bestcx - x, bestcy - y, bestinner, bestouter_lr,
             bestouter_ud, bestinner / bestouter, (int)t, min);
    return false;
  } else if (bestinner > bestouter * 8 * min_contrast &&
             dist < max_distance * max_distance / 128)
    *priority = 3;
  else if (bestinner > bestouter * 4 * min_contrast &&
           dist < max_distance * max_distance / 32)
    *priority = 2;
  else if (bestinner > bestouter * 2 * min_contrast)
    *priority = 1;
  else
    *priority = 0;
  if (verbose_confirm > 1)
    printf("given:%f %f best:%f %f inner:%f outer:%f %f ratio:%f priority:%i "
           "color:%i\n",
           x, y, bestcx - x, bestcy - y, bestinner, bestouter_lr, bestouter_ud,
           bestinner / bestouter, *priority, (int)t);
  return true;
}

/* Fixed-level priority queue used by flood_fill().  Higher numeric priorities
   are processed first so weak or misdetected patches are less likely to seed a
   large part of the screen.  */

template <int N, typename T> class priority_queue {
public:
  priority_queue() : sum{} {}
  const int npriorities = N;
  std::vector<T> queue[N];
  int sum[N];

  /* Insert E with numeric PRIORITY in the range zero through N - 1.  */
  void insert(T e, int priority) {
    sum[priority]++;
    queue[N - priority - 1].push_back(e);
  }

  /* Extract the next entry from the highest nonempty priority into E.  */
  bool extract_min(T &e) {
    for (int i = 0; i < npriorities; i++)
      if (queue[i].size()) {
        e = queue[i].back();
        queue[i].pop_back();
        return true;
      }
    return false;
  }

  /* Print insertion counts for every priority to F.  */
  void print_sums(FILE *f) const {
    fprintf(f, "Overall priority entries:");
    for (int n : sum) {
      fprintf(f, " %i", n);
    }
    fprintf(f, "\n");
  }
};

/* Return the Paget/Finlay patch color at diagonal screen coordinate X, Y.  */
static solver_parameters::point_color diagonal_coordinates_to_color(int x,
                                                                    int y) {
  if (!(y & 1))
    return (x & 1) ? solver_parameters::blue : solver_parameters::green;
  return (x & 1) ? solver_parameters::red : solver_parameters::blue;
}

/* Grow an initial regular-screen solution across IMG.  GREENX, GREENY is the
   image position assigned to screen coordinate zero and PARAM is the initial
   lattice transform.  FAST validates connected components in COLOR_MAP; SLOW
   validates sampled color contrast through RENDER.  Store optional solver
   points in SPARAM, use VISITED to avoid reusing classified components, return
   the number of accepted patches through NPATCHES, and apply limits from
   DSPARAMS.  REPORT_FILE receives diagnostics and PROGRESS reports work and
   cancellation.  Store a stable rejection identifier in FAILURE_REASON.
   Return null when the candidate cannot cover the required screen area
   consistently.  */
std::unique_ptr<screen_map> flood_fill(
    FILE *report_file, bool slow, bool fast, coord_t greenx, coord_t greeny,
    const scr_to_img_parameters &param, const image_data &img,
    const render_scr_detect *render, const color_class_map *color_map,
    solver_parameters *sparam, bitmap_2d *visited, int *npatches,
    const detect_regular_screen_params *dsparams, progress_info *progress,
    const char **failure_reason) {
  *failure_reason = "none";
  coord_t screen_xsize = my_sqrt(param.coordinate1.x * param.coordinate1.x +
                                 param.coordinate1.y * param.coordinate1.y);
  coord_t screen_ysize = my_sqrt(param.coordinate2.x * param.coordinate2.x +
                                 param.coordinate2.y * param.coordinate2.y);

  scr_detect::color_class my_red = scr_detect::red;
  scr_detect::color_class my_green = scr_detect::green;
  scr_detect::color_class my_blue = scr_detect::blue;

  if (param.type == DioptichromeB)
    std::swap(my_red, my_green);
  else if (param.type == ImprovedDioptichromeB || param.type == Omnicolore)
    std::swap(my_red, my_blue);

  /* If screen is estimated too small or too large give up.  */
  if (screen_xsize < 2 || screen_ysize < 2 || screen_xsize > 100 ||
      screen_ysize > 100) {
    *failure_reason = "screen-size";
    return NULL;
  }

  /* Do not flip the image.  */
  if (dufay_like_screen_p(param.type) && param.coordinate1.y < 0) {
    *failure_reason = "flipped-screen";
    return NULL;
  }

  scr_to_img scr_map;
  if (!scr_map.set_parameters(param, img)) {
    *failure_reason = "initial-map";
    return NULL;
  }

  int max_patch_size = my_floor(screen_xsize * screen_ysize / (coord_t)1.5);
  int min_patch_size = (int)(screen_xsize * screen_ysize / 8);

  if (paget_like_screen_p(param.type)) {
    /* Dufay has 4 square patches and one strip per screen tile, while
     * Paget/Finlay has 8 squares.  */
    // max_patch_size /= 2;
    // min_patch_size /= 2;
    /* Disable pixels on boundaries.  */
    min_patch_size = min_patch_size - 4 * my_sqrt((coord_t)min_patch_size);
    // fprintf (stderr, "Computed min patch size %i\n", min_patch_size);
  }
  min_patch_size = std::max(min_patch_size, 1);
  max_patch_size = std::max(max_patch_size, min_patch_size + 1);
  coord_t max_distance = (screen_xsize + screen_ysize) * 0.1;
  int nfound = 1;

  int_image_area range(scr_map.get_range(img.width, img.height));
  int xshift = range.xshift(), yshift = range.yshift(), width = range.width,
      height = range.height;
#if 0
  if (width <= xshift)
    width = xshift + 1;
  if (height <= yshift)
    height = yshift + 1;
#endif
  int nexpected = (paget_like_screen_p(param.type) ? 8 : 2) * img.width *
                  img.height / (screen_xsize * screen_ysize);
  // printf ("Flood fill started with coordinates %f,%f and %f,%f\n",
  // param.coordinate1.x, param.coordinate1.y, param.coordinate2.x,
  // param.coordinate2.y);
  /* Be sure that coordinates 0,0 are on screen.
     Normally, this should always be the case since screen discovery always
     places point 0,0 on screen. But in case of large deformations we may hit
     this.   */
  if (width <= xshift || height <= yshift) {
    *failure_reason = "seed-outside-map";
    return NULL;
  }
  if (report_file)
    fprintf(report_file,
            "Flood fill started with coordinates %f,%f and %f,%f\n",
            param.coordinate1.x, param.coordinate1.y, param.coordinate2.x,
            param.coordinate2.y);
  if (progress)
    progress->set_task("flood fill", nexpected);
  if (dufay_like_screen_p(param.type)) {
    xshift *= 2;
    width *= 2;
  } else {
    int xmin, ymin, xmax, ymax;
    analyze_base::data_entry p = paget_geometry::to_diagonal_coordinates(
        (analyze_base::data_entry){-xshift, -yshift});
    int ix = p.x / 2;
    int iy = p.y / 2;
    xmin = ix - 1;
    xmax = ix + 1;
    ymin = iy - 1;
    ymax = iy + 1;
    p = paget_geometry::to_diagonal_coordinates(
        (analyze_base::data_entry){-xshift + width, -yshift});
    ix = p.x / 2;
    iy = p.y / 2;
    xmin = std::min(xmin, ix - 1);
    xmax = std::max(xmax, ix + 1);
    ymin = std::min(ymin, iy - 1);
    ymax = std::max(ymax, iy + 1);
    p = paget_geometry::to_diagonal_coordinates(
        (analyze_base::data_entry){-xshift, -yshift + height});
    ix = p.x / 2;
    iy = p.y / 2;
    xmin = std::min(xmin, ix - 1);
    xmax = std::max(xmax, ix + 1);
    ymin = std::min(ymin, iy - 1);
    ymax = std::max(ymax, iy + 1);
    p = paget_geometry::to_diagonal_coordinates(
        (analyze_base::data_entry){-xshift + width, -yshift + height});
    ix = p.x / 2;
    iy = p.y / 2;
    xmin = std::min(xmin, ix - 1);
    xmax = std::max(xmax, ix + 1);
    ymin = std::min(ymin, iy - 1);
    ymax = std::max(ymax, iy + 1);
    xshift = -xmin * 2;
    yshift = -ymin * 2;
    width = (xmax - xmin) * 2;
    height = (ymax - ymin) * 2;
  }
  std::unique_ptr<screen_map> map(
      new screen_map(param.type, xshift, yshift, width, height));

  struct queue_entry {
    /* SCR_X and SCR_Y use screen-specific integer coordinates.  Dufay-like
       screens double X; Paget/Finlay screens use diagonal coordinates.  */
    int scr_x, scr_y;
    coord_t img_x, img_y;
  };
  priority_queue<N_PRIORITIES, queue_entry> queue;
  queue.insert((struct queue_entry){0, 0, greenx, greeny}, 0);
  map->set_coord({0, 0}, {greenx, greeny});
  if (sparam)
    sparam->remove_points();
  // printf ("%i %i %f %f %f %f\n", queue.size (), map.in_range_p (0, 0),
  // param.coordinate1.x, param.coordinate1.y, param.coordinate2.x,
  // param.coordinate2.y);
  queue_entry e;
  while (queue.extract_min(e) && (!progress || !progress->cancel_requested())) {
    coord_t ix, iy;
    int priority = 0;
    int priority2 = 0;
    // if (verbose)
    // printf ("visiting %i %i %f %f %f %f\n", e.scr_x, e.scr_y, e.img_x,
    // e.img_y, param.coordinate1.x, param.coordinate1.y);
    if (progress)
      progress->inc_progress();
    if (dufay_like_screen_p(param.type)) {
      if (sparam)
        sparam->add_point(
            {e.img_x, e.img_y}, {e.scr_x / 2.0, (coord_t)e.scr_y},
            (e.scr_x & 1)
                ? (colorscreen::solver_parameters::point_color)my_blue
                : (colorscreen::solver_parameters::point_color)my_green);

      // search range should be 1/2 but 1/3 seems to work better in
      // practice. Maybe it is because we look into orthogonal bounding
      // box of the area we really should compute.
#define cpatch(x, y, t, priority)                                              \
  ((fast && confirm_patch(report_file, color_map, x, y, t, min_patch_size,     \
                          max_patch_size, max_distance, &ix, &iy, &priority,   \
                          visited)) ||                                         \
   (slow && confirm(render, param.coordinate1, param.coordinate2, x, y, t,     \
                    color_map->width, color_map->height, max_distance, &ix,    \
                    &iy, &priority, 1.0f / 3.0f, 0.5f, 0.5f, false, false,     \
                    dsparams->min_patch_contrast)))
#define cstrip(x, y, t, priority)                                              \
  ((fast &&                                                                    \
    confirm_strip(color_map, x, y, t, min_patch_size, &priority, visited)) ||  \
   (slow && confirm(render, param.coordinate1, param.coordinate2, x, y, t,     \
                    color_map->width, color_map->height, max_distance, &ix,    \
                    &iy, &priority, 1.0f / 3.0f, 0.5f, 0.5f, true, false,      \
                    dsparams->min_patch_contrast)))
      if (!map->known_p({e.scr_x - 1, e.scr_y}) &&
          cpatch(e.img_x - param.coordinate1.x / 2,
                 e.img_y - param.coordinate1.y / 2,
                 ((e.scr_x - 1) & 1) ? my_blue : my_green, priority)) {
        map->safe_set_coord({e.scr_x - 1, e.scr_y}, {ix, iy});
        queue.insert((struct queue_entry){e.scr_x - 1, e.scr_y, ix, iy},
                     priority);
        nfound++;
      }
      if (!map->known_p({e.scr_x + 1, e.scr_y}) &&
          cpatch(e.img_x + param.coordinate1.x / 2,
                 e.img_y + param.coordinate1.y / 2,
                 ((e.scr_x + 1) & 1) ? my_blue : my_green, priority)) {
        map->safe_set_coord({e.scr_x + 1, e.scr_y}, {ix, iy});
        queue.insert((struct queue_entry){e.scr_x + 1, e.scr_y, ix, iy},
                     priority);
        nfound++;
      }
      if (!map->known_p({e.scr_x, e.scr_y - 1}) &&
          cstrip(e.img_x - param.coordinate2.x / 2,
                 e.img_y - param.coordinate2.y / 2, my_red, priority) &&
          cpatch(e.img_x - param.coordinate2.x, e.img_y - param.coordinate2.y,
                 (e.scr_x & 1) ? my_blue : my_green, priority2)) {
        map->safe_set_coord({e.scr_x, e.scr_y - 1}, {ix, iy});
        queue.insert((struct queue_entry){e.scr_x, e.scr_y - 1, ix, iy},
                     std::min(priority, priority2));
        nfound++;
      }
      if (!map->known_p({e.scr_x, e.scr_y + 1}) &&
          cstrip(e.img_x + param.coordinate2.x / 2,
                 e.img_y + param.coordinate2.y / 2, my_red, priority) &&
          cpatch(e.img_x + param.coordinate2.x, e.img_y + param.coordinate2.y,
                 (e.scr_x & 1) ? my_blue : my_green, priority2)) {
        map->safe_set_coord({e.scr_x, e.scr_y + 1}, {ix, iy});
        queue.insert((struct queue_entry){e.scr_x, e.scr_y + 1, ix, iy},
                     std::min(priority, priority2));
        nfound++;
      }
#undef cstrip
#undef cpatch
    } else {
      /* Blue patches are smaller.  */
      int blue_min_patch_size = (min_patch_size + 1) / 2;
      point_t c1 = param.coordinate2 - param.coordinate1;
      point_t c2 = param.coordinate1 + param.coordinate2;
#define cpatch(x, y, t, priority)                                              \
  ((fast && confirm_patch(report_file, color_map, x, y, t,                     \
                          t == scr_detect::blue ? blue_min_patch_size          \
                                                : min_patch_size,              \
                          max_patch_size, max_distance, &ix, &iy, &priority,   \
                          visited)) ||                                         \
   (slow && confirm(render, c1, c2, x, y, t, color_map->width,                 \
                    color_map->height, max_distance, &ix, &iy, &priority,      \
                    1.0 / 3, 0.20, t == scr_detect::blue ? 0.18 : 0.25, false, \
                    t == scr_detect::blue, dsparams->min_patch_contrast)))
      //|| (!fast && confirm (render, c1_x, c1_y, c2_x, c2_y, x, y, t,
      // color_map->width, color_map->height, max_distance, &ix, &iy,
      //&priority, 1.0 / 6, 0.33 / 2, 0.33 / 2, false)))
      if (sparam) {
        analyze_base::data_entry p = paget_geometry::from_diagonal_coordinates(
            (analyze_base::data_entry){e.scr_x, e.scr_y});
        solver_parameters::point_color color =
            diagonal_coordinates_to_color(e.scr_x, e.scr_y);
        if (sparam)
          sparam->add_point({e.img_x, e.img_y}, {p.x / 2.0, p.y / 2.0}, color);
      }
      for (int xx = -1; xx <= 1; xx++)
        for (int yy = -1; yy <= 1; yy++)
          if ((xx || yy) // && ((xx != 0) + (yy != 0)) == 1
              && !map->known_p({e.scr_x + xx, e.scr_y + yy})) {
            analyze_base::data_entry p =
                paget_geometry::from_diagonal_coordinates(
                    (analyze_base::data_entry){xx, yy});
            solver_parameters::point_color color =
                diagonal_coordinates_to_color(e.scr_x + xx, e.scr_y + yy);
            if (cpatch(e.img_x + p.x * param.coordinate1.x / 4 +
                           p.y * param.coordinate2.x / 4,
                       e.img_y + p.x * param.coordinate1.y / 4 +
                           p.y * param.coordinate2.y / 4,
                       (scr_detect::color_class)color, priority)) {
              map->safe_set_coord({e.scr_x + xx, e.scr_y + yy}, {ix, iy});
              queue.insert(
                  (struct queue_entry){e.scr_x + xx, e.scr_y + yy, ix, iy},
                  priority);
              nfound++;
            }
          }
#undef cpatch
    }
  }
  if (progress && progress->cancel_requested()) {
    *failure_reason = "cancelled";
    return NULL;
  }
  /* A Dufay-like screen has two square-patch centers per repetition.  */
  *npatches = nfound;
  if (nfound < 100) {
    *failure_reason = "too-few-patches";
    return NULL;
  }

  /* Refine the screen dimensions before computing coverage statistics.  */
  solver_parameters sparam2;
  scr_to_img_parameters param2;
  if (dsparams->do_mesh)
    sparam2.optimize_lens = sparam2.optimize_tilt = false;
  map->determine_solver_points(*npatches, &sparam2);
  param2 = param;
  if (simple_solver(&param2, img, sparam2, progress) > 1e20) {
    *failure_reason = "refine-solver";
    return NULL;
  }
  if (progress && progress->cancel_requested()) {
    *failure_reason = "cancelled";
    return NULL;
  }
  screen_xsize = my_sqrt(param2.coordinate1.x * param2.coordinate1.x +
                          param2.coordinate1.y * param2.coordinate1.y);
  screen_ysize = my_sqrt(param2.coordinate2.x * param2.coordinate2.x +
                          param2.coordinate2.y * param2.coordinate2.y);
  nexpected = (paget_like_screen_p(param.type) ? 8 : 2) * img.width *
              img.height / (screen_xsize * screen_ysize);

  /* Check for large unanalyzed areas.  */
  scr_to_img map2;
  if (!map2.set_parameters(param2, img)) {
    *failure_reason = "refined-map";
    return NULL;
  }
  int xmin, ymin, xmax, ymax;
  map->get_known_range(&xmin, &ymin, &xmax, &ymax);
  int snexpected = (paget_like_screen_p(param.type) ? 8 : 2) * (xmax - xmin) *
                   (ymax - ymin) / (screen_xsize * screen_ysize);
  if (snexpected > 0 && nfound > 1000) {
#if 0
      progress->pause_stdout ();
      printf ("Analyzed %2.2f%% of scan and %2.2f%% of the screen area", nfound * 100.0 / nexpected, nfound * 100.0 / snexpected);
      printf ("; left border: %2.2f%%", xmin * 100.0 / img.width);
      printf ("; top border: %2.2f%%", ymin * 100.0 / img.height);
      printf ("; right border: %2.2f%%", 100 - xmax * 100.0 / img.width);
      printf ("; bottom border: %2.2f%%", 100 - ymax * 100.0 / img.height);
      printf ("\n");
      progress->resume_stdout ();
#endif
    if (report_file)
      fprintf(report_file,
              "Analyzed %2.2f%% of scan and %2.2f%%  of the screen area",
              nfound * 100.0 / nexpected, nfound * 100.0 / snexpected);
    if (report_file)
      fprintf(report_file, "; left border: %2.2f%%", xmin * 100.0 / img.width);
    if (report_file)
      fprintf(report_file, "; top border: %2.2f%%", ymin * 100.0 / img.height);
    if (report_file)
      fprintf(report_file, "; right border: %2.2f%%",
              100 - xmax * 100.0 / img.width);
    if (report_file)
      fprintf(report_file, "; bottom border: %2.2f%%",
              100 - ymax * 100.0 / img.height);
    if (report_file) {
      fprintf(report_file, "\n");
      queue.print_sums(report_file);
    }
  }
  if (!dsparams->do_mesh && nfound > 100000)
    return map;

  if (progress)
    progress->set_task("checking known range", map->height);
  for (int y = -map->yshift; y < map->height - map->yshift; y++) {
    int last_seen = INT_MAX / 2;
    if (progress && progress->cancel_requested()) {
      *failure_reason = "cancelled";
      return NULL;
    }
    for (int x = -map->xshift; x < map->width - map->xshift; x++, last_seen++)
      if (!map->known_p({x, y})) {
        point_t scr = map->get_screen_coord({x, y});
        point_t img = map2.to_img(scr);
        if (img.x < xmin || img.x > xmax || img.y < ymin || img.y > ymax)
          continue;
        int xrmul = 2;
        int yrmul = paget_like_screen_p(param.type) ? 2 : 1;
        bool found = last_seen < dsparams->max_unknown_screen_range * xrmul;
        for (int yy = std::max(y - dsparams->max_unknown_screen_range * yrmul,
                               -map->yshift);
             yy < std::min(map->height - map->yshift,
                           y + dsparams->max_unknown_screen_range * yrmul) &&
             !found;
             yy++)
          for (int xx = std::max(x - dsparams->max_unknown_screen_range * xrmul,
                                 -map->xshift);
               xx < std::min(map->width - map->xshift,
                             x + dsparams->max_unknown_screen_range * xrmul) &&
               !found;
               xx++)
            if (map->known_p({xx, yy})) {
              last_seen = x - xx;
              found = true;
            }
        if (!found) {
          if (progress)
            progress->pause_stdout();
          printf("Too large unanalyzed unknown screen area around "
                 "image coordinates %f %f\n",
                 img.x, img.y);
          if (progress)
            progress->resume_stdout();
          if (report_file) {
            fprintf(report_file,
                    "Too large unanalyzed unknown screen area around "
                    "image coordinates %f %f\n",
                    img.x, img.y);
          }
          *failure_reason = "unknown-area";
          return NULL;
        }
        // else
        // printf ("found %i %i\n",x,y);
      } else
        last_seen = 0;
    if (progress)
      progress->inc_progress();
  }
  if (snexpected * dsparams->min_screen_percentage > nfound * 100) {
    if (report_file) {
      fprintf(report_file,
              "Detected screen patches cover only %2.2f%% of the screen\n",
              nfound * 100.0 / snexpected);
      // fprintf (report_file, "Reducing --min-screen-percentage would
      // bypass this error\n");
    }
    *failure_reason = "insufficient-coverage";
    return NULL;
  }
  if (xmin > std::max(dsparams->border_left, (coord_t)2) * img.width / 100) {
    if (report_file)
      fprintf(report_file,
              "Detected screen failed to reach left border of the image "
              "(limit %f)\n",
              dsparams->border_left);
    *failure_reason = "left-border";
    return NULL;
  }
  if (ymin > std::max(dsparams->border_top, (coord_t)2) * img.height / 100) {
    if (report_file)
      fprintf(report_file,
              "Detected screen failed to reach top border of the image "
              "(limit %f)\n",
              dsparams->border_top);
    *failure_reason = "top-border";
    return NULL;
  }
  if (xmax <
      std::min(100 - dsparams->border_right, (coord_t)98) * img.width / 100) {
    if (report_file)
      fprintf(report_file,
              "Detected screen failed to reach right border of the image "
              "(limit %f)\n",
              dsparams->border_right);
    *failure_reason = "right-border";
    return NULL;
  }
  if (ymax <
      std::min(100 - dsparams->border_bottom, (coord_t)98) * img.height / 100) {
    if (report_file)
      fprintf(report_file,
              "Detected screen failed to reach bottom border of the image "
              "(limit %f)\n",
              dsparams->border_bottom);
    *failure_reason = "bottom-border";
    return NULL;
  }
  return map;
}

/* Return every cell in an XSTEPS by YSTEPS search grid, ordered in
   concentric square rings from the central cell toward the image border.  */
std::vector<int_point_t> check_points(int xsteps, int ysteps) {
  std::vector<int_point_t> ret;
  ret.push_back({xsteps / 2, ysteps / 2});
  for (int d = 1; d < std::max(xsteps, ysteps); d++) {
    for (int i = -d; i <= d; i++) {
      int xx = xsteps / 2 + i;
      if (xx < 0 || xx >= xsteps)
        continue;
      int yy = ysteps / 2 - d;
      if (yy >= 0 && yy < ysteps)
        ret.push_back({xx, yy});
      yy = ysteps / 2 + d;
      if (yy >= 0 && yy < ysteps)
        ret.push_back({xx, yy});
    }
    for (int i = -d + 1; i < d; i++) {
      int yy = ysteps / 2 + i;
      if (yy < 0 || yy >= ysteps)
        continue;
      int xx = xsteps / 2 - d;
      if (xx >= 0 && xx < xsteps)
        ret.push_back({xx, yy});
      xx = xsteps / 2 + d;
      if (xx >= 0 && xx < xsteps)
        ret.push_back({xx, yy});
    }
  }
  return ret;
}

/* Summarize distances between detected coordinates in SMAP and the transform
   PARAM for IMG.  TYPE names the transform in REPORT_FILE and PROGRESS is
   paused while the report is written.  */
void summarise_quality(const image_data &img, const screen_map *smap,
                       const scr_to_img_parameters &param, const char *type,
                       FILE *report_file, progress_info *progress) {
  coord_t max_distance[3] = {0, 0, 0};
  coord_t distance_sum[3] = {0, 0, 0};
  int distance_num[3] = {0, 0, 0};
  int one_num[3] = {0, 0, 0};
  int four_num[3] = {0, 0, 0};
  scr_to_img map;
  if (!map.set_parameters(param, img))
    return;
  for (int y = -smap->yshift; y < smap->height - smap->yshift; y++)
    for (int x = -smap->xshift; x < smap->width - smap->xshift; x++)
      if (smap->known_and_not_fake_p({x, y})) {
        solver_parameters::point_color color;
        point_t scrp = smap->get_screen_coord({x, y}, &color);
        point_t imgp = map.to_img(scrp);
        point_t imgp2 = smap->get_coord({x, y});
        coord_t dist = imgp.dist_from(imgp2);
        int t = (int)color;
        max_distance[t] = std::max(max_distance[t], dist);
        distance_sum[t] += dist;
        distance_num[t]++;
        if (dist >= 4)
          four_num[t]++;
        else if (dist >= 1)
          one_num[t]++;
      }
  if (progress)
    progress->pause_stdout();
  for (int c = 0; c < 3; c++)
    if (distance_num[c]) {
      const char *channel[3] = {"Red", "Green", "Blue"};
#if 0
        printf ("%s patches %i. Avg distance to %s solution %f; max distance %f; %2.2f%% with distance over 1 and %2.2f%% with distance over 4\n", channel[c], distance_num[c], type, distance_sum[c] / distance_num[c], max_distance[c], (one_num[c] + four_num[c]) * 100.0 / distance_num[c], four_num[c] * 100.0 / distance_num[c]);
#endif
      if (report_file)
        fprintf(report_file,
                "%s patches %i. Avg distance to %s solution %f; max "
                "distance %f; %2.2f%% with distance at least 1 and %2.2f%% "
                "with distance at least 4\n",
                channel[c], distance_num[c], type,
                distance_sum[c] / distance_num[c], max_distance[c],
                (one_num[c] + four_num[c]) * 100.0 / distance_num[c],
                four_num[c] * 100.0 / distance_num[c]);
    }
  if (progress)
    progress->resume_stdout();
}

/* Detect a regular screen in IMG using color classification DPARAM and limits
   DSPARAMS.  Search candidate regions from the center outward, estimate an
   initial lattice for each supported regular screen, flood-fill accepted
   patches, and fit the final homography or mesh.  Update SPARAM with final
   solver points, report optional diagnostics to REPORT_FILE, and use PROGRESS
   for status and cancellation.  Return an unsuccessful result on any rejected
   or cancelled stage.  */
detected_screen
detect_regular_screen_1(const image_data &img, scr_detect_parameters &dparam,
                        solver_parameters &sparam,
                        const detect_regular_screen_params *dsparams,
                        progress_info *progress, FILE *report_file) {
  /* Try all supported regular-screen families.  */
  const bool try_dufay = true;
  const bool try_omnicolore = true;
  const bool try_paget_finlay = true;
  // report_file = stdout;

  detected_screen ret;
  render_parameters empty;
  std::unique_ptr<screen_map> smap = NULL;
  scr_type type = dsparams->scr_type;
  assert(dsparams->scanner_type != max_scanner_type);

  empty.gamma = dsparams->gamma;
  assert(empty.gamma != 0 || img.to_linear[0].size());
  ret.mesh_trans = NULL;
  ret.success = false;
  ret.known_patches = NULL;
  ret.smap = NULL;
  ret.param.type = type;
  ret.param.scanner_type = dsparams->scanner_type;
  scr_to_img_parameters param;
  param.type = type;
  param.scanner_type = dsparams->scanner_type;
  detection_stats stats(report_file);
  stats.type = type;
  stats.legacy_preclassification_sharpening =
      dparam.sharpen_radius > 0 && dparam.sharpen_amount > 0;

  {
    bitmap_2d visited(img.width, img.height);
    bitmap_2d visited_paget(img.width, img.height);
    bitmap_2d visited_paget2(img.width, img.height);
    bitmap_2d visited_dioptichromeB(img.width, img.height);
    bitmap_2d visited_improved_dioptichromeB(img.width, img.height);
    bitmap_2d visited_omnicolore(img.width, img.height);
    std::unique_ptr<render_scr_detect> render = NULL;
    std::unique_ptr<color_class_map> cmap = NULL;
    const int search_xsteps = 6;
    const int search_ysteps = 6;

    /* We try to detect screen starting from various places in the scans
       organized from center to the border.  */
    auto points = check_points(search_xsteps, search_ysteps);

    if (progress)
      progress->set_task("looking for initial grid",
                         search_xsteps * search_ysteps);
    for (int s = 0; s < (int)points.size() && !smap; s++)
      if (!progress || !progress->cancel_requested()) {
        stats.regions++;
        int xmin = points[s].x * img.width / search_xsteps;
        int ymin = points[s].y * img.height / search_ysteps;
        int xmax = (points[s].x + 1) * img.width / search_xsteps;
        int ymax = (points[s].y + 1) * img.height / search_ysteps;
        int nattempts = 0;
        const int maxattempts = 10;
        if (report_file)
          fflush(report_file);
        if (progress)
          progress->push();
        if (dsparams->optimize_colors) {
          detection_stats::time_point stage_start = stats.start_timer();
          bool colors_ok = optimize_screen_colors(
              &dparam, &img, dsparams->gamma,
              {xmin, ymin, std::min(1000, xmax - xmin),
               std::min(1000, ymax - ymin)},
              progress, report_file);
          stats.add_time(&stats.optimize_colors_ms, stage_start);
          if (!colors_ok) {
            stats.color_opt_failures++;
            if (progress)
              progress->pause_stdout();
            printf("Failed to analyze colors on start coordinates %i,%i "
                   "(translated %i,%i) failed (%i out of %i attempts)\n",
                   (int)points[s].x, (int)points[s].y, xmax, ymax, s + 1,
                   (int)points.size());
            if (report_file)
              fprintf(report_file,
                      "Failed to analyze colors on start coordinates %i,%i "
                      "(translated %i,%i) failed (%i out of %i attempts)\n",
                      (int)points[s].x, (int)points[s].y, xmax, ymax, s + 1,
                      (int)points.size());
            if (progress)
              progress->resume_stdout();
            if (progress) {
              progress->pop();
              progress->inc_progress();
            }
            continue;
          }
          /* Re-detect screen.  */
          cmap = NULL;
          render = NULL;
        }
        if (!render) {
          prune_render_scr_detect_caches();
          detection_stats::time_point stage_start = stats.start_timer();
          std::unique_ptr<render_scr_detect> new_render(
              new render_scr_detect(dparam, img, empty, 256));
          if (!new_render) {
            stats.add_time(&stats.precompute_ms, stage_start);
            stats.precompute_failures++;
            stats.result = "renderer-allocation";
            if (progress)
              progress->pop();
            return ret;
          }
          render = std::move(new_render);
          bool precompute_ok = render->precompute_all(PRECOMPUTE_NONE, progress);
          if (precompute_ok && stats.legacy_preclassification_sharpening)
            stats.rgb_precomputes++;
          if (precompute_ok && dsparams->slow_floodfill) {
            precompute_ok = render->precompute_rgbdata(progress);
            if (precompute_ok
                && !stats.legacy_preclassification_sharpening)
              stats.rgb_precomputes++;
          }
          stats.add_time(&stats.precompute_ms, stage_start);
          if (!precompute_ok) {
            stats.precompute_failures++;
            render = NULL;
            if (progress) {
              progress->pop();
              progress->inc_progress();
            }
            continue;
          }
        }
        /* In Paget/Finlay screens, blue patches touch at their corners.
           The separately materialized class map was intended to enforce
           boundaries between them, but small blue patches may disappear.
           Keep both this map and the renderer's original map as candidates.  */
        if (try_paget_finlay) {
          detection_stats::time_point stage_start = stats.start_timer();
          stats.classmap_builds++;
          std::unique_ptr<color_class_map> new_cmap(new color_class_map);
          cmap = std::move(new_cmap);
          cmap->allocate(img.width, img.height);
          if (progress)
            progress->set_task("pruning screen", img.height);
#pragma omp parallel for default(none) shared(progress, img, cmap, render)
          for (int y = 0; y < img.height; y++) {
            if (!progress || !progress->cancel_requested())
              for (int x = 0; x < img.width; x++)
                cmap->set_class(x, y, render->classify_pixel({x, y}));
            if (progress)
              progress->inc_progress();
          }
          stats.add_time(&stats.classmap_ms, stage_start);
          if (progress && progress->cancel_requested()) {
            stats.result = "cancelled";
            if (progress)
              progress->pop();
            return ret;
          }
        }
        if (progress)
          progress->set_task("looking for initial green patch", 1);
        if (!progress || !progress->cancel_requested())
          for (int y = ymin; y < ymax && !smap && nattempts < maxattempts; y++)
            for (int x = xmin; x < xmax && !smap && nattempts < maxattempts;
                 x++) {
              if (report_file)
                stats.seed_pixels++;
              if (report_file)
                fflush(report_file);

              enum scr_type current_type = Random;
              color_class_map *this_cmap;
              /* Try to guess both screen types.  If we find Paget/Finlay
                 screen, preserve original type if it makes sense,
                 otherwise default to Paget.  */
              if (try_dufay &&
                  try_guess_screen(report_file, Dufay,
                                   *render->get_color_class_map(), sparam, x, y,
                                   &visited)) {
                current_type = Dufay;
                this_cmap = render->get_color_class_map();
              } else if (try_dufay &&
                         try_guess_screen(report_file, DioptichromeB,
                                          *render->get_color_class_map(),
                                          sparam, x, y,
                                          &visited_dioptichromeB)) {
                current_type = DioptichromeB;
                this_cmap = render->get_color_class_map();
              } else if (try_dufay &&
                         try_guess_screen(
                             report_file, ImprovedDioptichromeB,
                             *render->get_color_class_map(), sparam, x, y,
                             &visited_improved_dioptichromeB)) {
                current_type = ImprovedDioptichromeB;
                this_cmap = render->get_color_class_map();
              } else if (try_omnicolore &&
                         try_guess_screen(
                             report_file, Omnicolore,
                             *render->get_color_class_map(), sparam, x, y,
                             &visited_omnicolore)) {
                current_type = Omnicolore;
                this_cmap = render->get_color_class_map();
              } else if (try_paget_finlay &&
                         try_guess_paget_screen(
                             report_file,
                             cmap ? *cmap : *render->get_color_class_map(),
                             sparam, x, y, &visited_paget)) {
                current_type = type == Finlay ? Finlay : Paget;
                this_cmap = cmap ? cmap.get() : render->get_color_class_map();
              } else if (try_paget_finlay && cmap &&
                         try_guess_paget_screen(
                             report_file, *render->get_color_class_map(),
                             sparam, x, y, &visited_paget2)) {
                current_type = type == Finlay ? Finlay : Paget;
                this_cmap = render->get_color_class_map();
              }
              if (progress && progress->cancel_requested()) {
                stats.result = "cancelled";
                if (progress)
                  progress->pop();
                return ret;
              }

              if (current_type != Random) {
                stats.initial_grids++;
                nattempts++;
                if (report_file && verbose) {
                  fprintf(report_file, "Initial grid found at:\n");
                  sparam.dump(report_file);
                }
                visited.clear();
                param.type = current_type;
                detection_stats::time_point stage_start = stats.start_timer();
                coord_t initial_solver_error =
                    simple_solver(&param, img, sparam, progress);
                stats.add_time(&stats.initial_solver_ms, stage_start);
                if (initial_solver_error > (coord_t)1e20) {
                  stats.initial_solver_failures++;
                  continue;
                }
                stats.flood_attempts++;
                const char *flood_failure = "none";
                stage_start = stats.start_timer();
                smap =
                    flood_fill(report_file, dsparams->slow_floodfill,
                               dsparams->fast_floodfill, sparam.points[0].img.x,
                               sparam.points[0].img.y, param, img, render.get(),
                               this_cmap, NULL /*sparam*/, &visited,
                               &ret.patches_found, dsparams, progress,
                               &flood_failure);
                stats.add_time(&stats.flood_ms, stage_start);
                stats.last_flood_failure = flood_failure;
                if (!smap) {
                  stats.flood_failures++;
                  if (progress) {
                    progress->set_task("looking for initial grid",
                                       search_xsteps * search_ysteps);
                    progress->set_progress(s);
                  }
                  visited.clear();
                  x += 10;
                } else {
                  type = current_type;
                  stats.type = type;
                  stats.last_flood_failure = "none";
                  break;
                }
              }
            }
        if (!smap) {
          if (progress)
            progress->pause_stdout();
          printf("Start coordinates %i,%i (translated %i,%i) failed (%i "
                 "out of %i attempts)\n",
                 (int)points[s].x, (int)points[s].y, xmax, ymax, s + 1,
                 (int)points.size());
          if (report_file)
            fprintf(report_file,
                    "Start coordinates %i,%i (translated %i,%i) failed "
                    "(%i out of %i attempts)\n",
                    (int)points[s].x, (int)points[s].y, xmax, ymax, s + 1,
                    (int)points.size());
          if (progress)
            progress->resume_stdout();
        }
        if (progress) {
          progress->pop();
          progress->inc_progress();
        }
      }
#if 0
	  int max_diam = std::max (img.width, img.height);
	  for (int d = 0; d < max_diam && !smap; d++)
	  {
		  if (!progress || !progress->cancel_requested ())
			  for (int i = -d; i < d && !smap; i++)
			  {
				  if (try_guess_screen (report_file, *render.get_color_class_map (), sparam, img.width / 2 + i, img.height / 2 + d, &visited)
						  || try_guess_screen (report_file, *render.get_color_class_map (), sparam, img.width / 2 + i, img.height / 2 - d, &visited)
						  || try_guess_screen (report_file, *render.get_color_class_map (), sparam, img.width / 2 + d, img.height / 2 + i, &visited)
						  || try_guess_screen (report_file, *render.get_color_class_map (), sparam, img.width / 2 - d, img.height / 2 + i, &visited))
				  {
					  if (verbose)
					  {
						  if (report_file && verbose)
							  fprintf (report_file, "Initial grid found at:\n");
						  sparam.dump (report_file);
					  }
					  visited.clear ();
					  simple_solver (&param, img, sparam, progress);
					  smap = flood_fill (report_file, sparam.point[0].img_x, sparam.point[0].img_y, param, img, &render, render.get_color_class_map (), NULL /*sparam*/, &visited, &ret.patches_found, progress);
					  if (!smap)
					  {
						  if (progress)
						  {
							  progress->set_task ("Looking for initial grid", max_diam);
							  progress->set_progress (d);
						  }
						  visited.clear ();
					  }
					  else
						  break;
				  }
			  }
		  if (progress)
			  progress->inc_progress ();
	  }
#endif
  }
  if (!smap || (progress && progress->cancel_requested())) {
    if (progress && progress->cancel_requested())
      stats.result = "cancelled";
    else if (stats.flood_attempts)
      stats.result = "flood-fill";
    else
      stats.result = "no-initial-grid";
    stats.patches = ret.patches_found;
    return ret;
  }
  /* Obtain more realistic solution so the range chosen for final mesh is
   * likely right.  */
  if (progress)
    progress->set_task("obtaining initial solver points", 1);
  smap->determine_solver_points(ret.patches_found, &sparam);

  /* Determine scr-to-img parameters.
     Do perspective correction this time since this will be the final parameter
     produced.  */
  ret.param.type = type;
  /*ret.param.lens_center_x = img.width / 2;
    ret.param.lens_center_y = img.width / 2;*/
  // ret.param.projection_distance = img.width;
  ret.param.lens_correction = dsparams->lens_correction;
  detection_stats::time_point stage_start = stats.start_timer();
  coord_t solver_error = solver(&ret.param, img, sparam, progress);
  stats.add_time(&stats.final_solver_ms, stage_start);
  if (progress && progress->cancel_requested()) {
    stats.result = "cancelled";
    return ret;
  }
  if (!my_isfinite(solver_error) || solver_error > (coord_t)1e20) {
    stats.result = "final-solver";
    if (report_file)
      fprintf(report_file, "Final geometry solver failed\n");
    return ret;
  }
  summarise_quality(img, smap.get(), ret.param, "homographic", report_file,
                    progress);
  if (progress && progress->cancel_requested()) {
    stats.result = "cancelled";
    return ret;
  }
  if (report_file) {
    fprintf(report_file, "Detected geometry\n");
    if (!save_csp(report_file, &ret.param, NULL, NULL, NULL)) {
      /* Ignore failure, but at least we checked it.  */
    }
  }
  {
    render_to_scr render(ret.param, img, empty, 256);
    ret.pixel_size = render.pixel_size();
    if (report_file)
      fprintf(report_file, "pixel size: %f\n", ret.pixel_size);
  }
  int xmin_r, ymin_r, xmax_r, ymax_r;
  smap->get_known_range(&xmin_r, &ymin_r, &xmax_r, &ymax_r);
  ret.range = {xmin_r, ymin_r, xmax_r - xmin_r, ymax_r - ymin_r};
  if (progress)
    progress->set_task("checking screen consistency", 1);
  int errs;
  if (dufay_like_screen_p(type))
    errs = smap->check_consistency(
        report_file, ret.param.coordinate1.x / 2, ret.param.coordinate1.y / 2,
        ret.param.coordinate2.x, ret.param.coordinate2.y,
        my_sqrt(ret.param.coordinate1.x * ret.param.coordinate1.x +
                ret.param.coordinate1.y * ret.param.coordinate1.y) /
            (coord_t)2);
  else
    errs = smap->check_consistency(
        report_file, (ret.param.coordinate1.x - ret.param.coordinate2.x) / 4,
        (ret.param.coordinate1.y - ret.param.coordinate2.y) / 4,
        (ret.param.coordinate1.x + ret.param.coordinate2.x) / 4,
        (ret.param.coordinate1.y + ret.param.coordinate2.y) / 4,
        my_sqrt(ret.param.coordinate1.x * ret.param.coordinate1.x +
                ret.param.coordinate1.y * ret.param.coordinate1.y) /
            (coord_t)3);
  /* If we do mesh, insert fake control points to the detected screen so the
   * binding tapes are not curly.  */
  if (dsparams->do_mesh && (dsparams->left || dsparams->top ||
                            dsparams->right || dsparams->bottom)) {
    scr_to_img map;
    if (!map.set_parameters(ret.param, img)) {
      stats.result = "straightening-map";
      return ret;
    }
    const int range = 10;
    if (progress)
      progress->set_task("straightening corners", smap->height);
    for (int y = -smap->yshift; y < smap->height - smap->yshift; y++) {
      int last_seen = INT_MAX / 2;
      for (int x = -smap->xshift; x < smap->width - smap->xshift;
           x++, last_seen++)
        if (!smap->known_p({x, y})) {
          int xrmul = 2;
          int yrmul = paget_like_screen_p(type) ? 2 : 1;
          bool found = last_seen < range * xrmul;
          for (int yy = std::max(y - range * yrmul, -smap->yshift);
               yy < std::min(smap->height - smap->yshift, y + range * yrmul) &&
               !found;
               yy++)
            for (int xx = std::max(x - range * xrmul, -smap->xshift);
                 xx < std::min(smap->width - smap->xshift, x + range * xrmul) &&
                 !found;
                 xx++)
              if (smap->known_p({xx, yy}))
                found = true;
          if (!found) {
            point_t scrp = smap->get_screen_coord({x, y});
            point_t imgp = map.to_img(scrp);
            last_seen = 0;
            if ((imgp.x <= ret.range.x && dsparams->left) ||
                (imgp.y < ret.range.y && dsparams->top) ||
                (imgp.x >= ret.range.x + ret.range.width && dsparams->right) ||
                (imgp.y >= ret.range.y + ret.range.height && dsparams->bottom))
              smap->set_coord({x, y}, imgp);
          }
          // else
          // printf ("found %i %i\n",x,y);
        } else
          last_seen = 0;
      if (progress)
        progress->inc_progress();
    }
  }
  if (errs) {
    if (progress)
      progress->pause_stdout();
    printf("%i inconsistent screen coordinates!\n", errs);
    if (progress)
      progress->resume_stdout();
  }
  if (dsparams->do_mesh) {
    stage_start = stats.start_timer();
    std::shared_ptr<mesh> m =
        solver_mesh(&ret.param, img, sparam, *smap, progress);
    stats.add_time(&stats.mesh_solver_ms, stage_start);
    if (!m || (progress && progress->cancel_requested())) {
      stats.result = progress && progress->cancel_requested() ? "cancelled"
                                                             : "mesh-solver";
      return ret;
    }
    const int xsteps = 50, ysteps = 50;
    m->precompute_inverse();
    /* Now produce output (regular) grid of solver points.
       This can be used to re-compute the mesh from GUI  */
    if (progress)
      progress->set_task("determining solver points", 1);
    sparam.remove_points();
    for (int y = 0; y < ysteps; y++)
      for (int x = 0; x < xsteps; x++) {
        int border = 1;
        point_t p =
            m->invert({(x + border) * img.width / (coord_t)(xsteps + border),
                       (y + border) * img.height / (coord_t)(ysteps + border)});
        p.x = nearest_int(p.x);
        p.y = nearest_int(p.y);
        point_t imgp = m->apply(p);
        if (sparam.find_img(imgp) < 0)
          sparam.add_point(imgp, p, solver_parameters::green);
      }
    ret.mesh_trans = m;
    ret.param.mesh_trans_is_scr_to_img = true;
    ret.param.mesh_trans = m;
    summarise_quality(img, smap.get(), ret.param, "mesh", report_file,
                      progress);
    ret.param.mesh_trans = NULL;
  } else
    ret.mesh_trans = NULL;

  /* Known patches is a bitmap in screen coordinates that is set of 1 if any
     patches belonging to a given screen coordinate was found.  */
  if (dsparams->return_known_patches) {
    if (progress)
      progress->set_task("computing known patches", 1);
    /* TODO: test that Dufay path can be replaced by generic one.  */
    if (dufay_like_screen_p(type)) {
      ret.xshift = smap->xshift / 2;
      ret.yshift = smap->yshift;
      ret.known_patches = new bitmap_2d(smap->width / 2, smap->height);
      for (int y = 0; y < smap->height; y++)
        for (int x = 0; x < smap->width / 2; x++)
          if (smap->known_p({x * 2 - ret.xshift * 2, y - ret.yshift}) &&
              smap->known_p({x * 2 - ret.xshift * 2 + 1, y - ret.yshift}))
            ret.known_patches->set_bit(x, y);
    } else {
      int xmin = INT_MAX, xmax = INT_MIN, ymin = INT_MAX, ymax = INT_MIN;
      for (int y = 0; y < smap->height; y++)
        for (int x = 0; x < smap->width; x++)
          if (smap->known_p({x - smap->xshift, y - smap->yshift})) {
            point_t scrp =
                smap->get_screen_coord({x - smap->xshift, y - smap->yshift});
            xmin = std::min(xmin, (int)scrp.x);
            xmax = std::max(xmax, (int)scrp.x);
            ymin = std::min(ymin, (int)scrp.y);
            ymax = std::max(ymax, (int)scrp.y);
          }
      ret.xshift = -xmin;
      ret.yshift = -ymin;
      ret.known_patches = new bitmap_2d(xmax - xmin + 1, ymax - ymin + 1);
      for (int y = 0; y < smap->height; y++)
        for (int x = 0; x < smap->width; x++)
          if (smap->known_p({x - smap->xshift, y - smap->yshift})) {
            point_t scr =
                smap->get_screen_coord({x - smap->xshift, y - smap->yshift});
            /* TODO: perhaps we should be conservative here and require
               all entries to be set.  But most likely this makes no
               difference.  */
            ret.known_patches->set_bit((int)scr.x + ret.xshift,
                                       (int)scr.y + ret.yshift);
          }
    }
  }
  if (progress)
    progress->pause_stdout();
  if (report_file)
    fprintf(report_file,
            "Unanalyzed border left: %f%%, right %f%%, top %f%%, bottom %f%%\n",
            ret.range.x * 100.0 / img.width,
            100 - (ret.range.x + ret.range.width) * 100.0 / img.width,
            ret.range.y * 100.0 / img.height,
            100 - (ret.range.y + ret.range.height) * 100.0 / img.height);
  if (progress)
    progress->resume_stdout();

  if (dsparams->return_screen_map)
    ret.smap = smap.release();
  stats.result = "success";
  stats.type = type;
  stats.patches = ret.patches_found;
  ret.success = true;
  return ret;
}
} // namespace

/* Detect a regular screen and prune temporary render-detection caches before
   returning.  IMG, DPARAM, SPARAM, DSPARAMS, PROGRESS, and REPORT_FILE have the
   same roles as in detect_regular_screen_1().  */
detected_screen
detect_regular_screen(const image_data &img, scr_detect_parameters &dparam,
                      solver_parameters &sparam,
                      const detect_regular_screen_params *dsparams,
                      progress_info *progress, FILE *report_file) {
  // dsparams->slow_floodfill = false;
  // dsparams->fast_floodfill = true;
  // dsparams->optimize_colors = false;
  detected_screen ret = detect_regular_screen_1(img, dparam, sparam, dsparams,
                                                progress, report_file);
  prune_render_scr_detect_caches();
  return ret;
}
} // namespace colorscreen
