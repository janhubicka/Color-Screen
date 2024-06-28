#include <cstring>
#include <vector>
#include "include/render-to-scr.h"
#include "include/analyze-base.h"
#include "include/tiff-writer.h"


static void
add(unsigned char ov[256][256][3], int c, double x, double y)
{
  int xx = x * 255.5;
  int yy = y * 255.5;
  if (xx < 0)
	  xx = 0;
  if (xx > 255)
	  xx = 255;
  if (yy < 0)
	  yy = 0;
  if (yy > 255)
	  yy = 255;
  if (ov[xx][yy][c]<255)
	  ov[xx][yy][c]++;
}

bool
analyze_base::find_best_match_using_cpfind (analyze_base &other, coord_t *xshift_ret, coord_t *yshift_ret, int direction, scr_to_img &map, scr_to_img &other_map, int scale, FILE *report_file, progress_info *progress)
{
  /* Top left corner of other scan in screen coordinates.  */
  coord_t lx, ly;
  other_map.to_scr (0, 0, &lx, &ly);
  FILE *f = fopen (direction ? "project-cpfind-vert.pto" : "project-cpfind-hor.pto","wt");
  if (!f)
    return false;
  int mwidth = std::max (m_width, other.m_width);
  int mheight = std::max (m_height, other.m_height);
  luminosity_t rmin, rmax, gmin, gmax, bmin, bmax;
  luminosity_t rmin2, rmax2, gmin2, gmax2, bmin2, bmax2;
  analyze_range (&rmin, &rmax, &gmin, &gmax, &bmin, &bmax);
  other.analyze_range (&rmin2, &rmax2, &gmin2, &gmax2, &bmin2, &bmax2);
  rmin = std::min (rmin, rmin2);
  rmax = std::max (rmax, rmax2);
  gmin = std::min (gmin, gmin2);
  gmax = std::max (gmax, gmax2);
  bmin = std::min (bmin, bmin2);
  bmax = std::max (bmax, bmax2);
  const char *error;
  const char *screen1 = direction ? "screen1-vert.tif" : "screen1-hor.tif";
  const char *screen2 = direction ? "screen2-vert.tif" : "screen2-hor.tif";
  if (!write_screen (screen1, NULL, &error, progress, rmin, rmax, gmin, gmax, bmin, bmax)
      || !other.write_screen (screen2, NULL, &error, progress, rmin, rmax, gmin, gmax, bmin, bmax))
    {
      if (progress)
	progress->pause_stdout ();
      fprintf (stderr, "Failed to write screen\n");
      if (progress)
	progress->resume_stdout ();
      return false;
    }
  fprintf (f, "p f2 w3000 h1500 v360  k0 E0 R0 n\"TIFF_m c:LZW r:CROP\"\n m i0\n"
	      "i w%i h%i f0 v28.53 Ra0 Rb0 Rc0 Rd0 Re0 Eev0 Er1 Eb1 r0 p0 y0 TrX0 TrY0 TrZ0 Tpy0 Tpp0 j0 a0 b0 c0 d0 e0 g0 t0 Va1 Vb0 Vc0 Vd0 Vx0 Vy0  Vm5 n\"%s\"\n"
	      "i w%i h%i f0 v=0 Ra=0 Rb=0 Rc=0 Rd=0 Re=0 Eev0 Er1 Eb1 r0 p0 y0 TrX0.334802534835735 TrY0.00196425708222156 TrZ0 Tpy-0 Tpp0 j0 a=0 b=0 c=0 d=0 e=0 g=0 t=0 Va=0 Vb=0 Vc=0 Vd=0 Vx=0 Vy=0  Vm5  n\"%s\"\n"
	      "v TrX1\n"
	      "v TrY1\n", /*m_width, m_height,*/ mwidth * scale, mheight * scale, /*filename1*/screen1, /*other.m_width, other.m_height,*/ mwidth * scale, mheight * scale, /*filename2*/screen2);
  fclose (f);
  /* cpfind ransac does not understand the setup where camera shifts.  But use it as last resort.  */
  for (int m = 0; m < 2; m++)
    {
      if (progress)
	progress->set_task ("executing cpfind", 1);
      char cmd[256];
      cmd[255]=0;
      snprintf (cmd, 254, "cpfind --fullscale %s project-cpfind-%s.pto -o project-cpfind-%s-out.pto >cpfind.out", m ? "" : "--ransacmode=rpy", direction ? "vert" : "hor", direction ? "vert" : "hor");
      //if (system (direction ? "cpfind --fullscale --ransacmode=rpy project-cpfind-vert.pto -o project-cpfind-vert-out.pto >cpfind.out": "cpfind --fullscale --ransacmode=rpy project-cpfind-hor.pto -o project-cpfind-hor-out.pto >cpfind.out"))
      if (system (cmd))
	{
	  if (progress)
	    progress->pause_stdout ();
	  fprintf (stderr, "Failed to execute cpfind\n");
	  if (progress)
	    progress->resume_stdout ();
	  return false;
	}
      f = fopen (direction ? "project-cpfind-vert-out.pto" : "project-cpfind-hor-out.pto", "r");
      if (!f)
	{
	  if (progress)
	    progress->pause_stdout ();
	  fprintf (stderr, "Failed to open cpfind output file\n");
	  if (progress)
	    progress->resume_stdout ();
	  return false;
	}
      struct offsets {
	int x, y, n;
      };
      struct bad_offsets {
	coord_t x, y;
      };
      std::vector<offsets> off;
      std::vector<bad_offsets> bad_off;
      int npoints = 0;
      while (!feof (f))
	{
	  int c;
	  if ((c = fgetc (f)) != 'c'
	      || (c = fgetc (f)) != ' '
	      || (c = fgetc (f)) != 'n'
	      || (c = fgetc (f)) != '0'
	      || (c = fgetc (f)) != ' '
	      || (c = fgetc (f)) != 'N'
	      || (c = fgetc (f)) != '1'
	      || (c = fgetc (f)) != ' '
	      || (c = fgetc (f)) != 'x')
	    {
	      while (c != EOF && c != '\n')
		c = fgetc (f);
	      continue;
	    }
	  float x1, y1, x2, y2;
	  npoints++;
	  if (fscanf (f, "%f", &x1) != 1)
	    {
	      if (progress)
		progress->pause_stdout ();
	      fprintf (stderr, "Error parsing cpfind's pto file 1\n");
	      if (progress)
		progress->resume_stdout ();
	      return false;
	    }
	  if ((c = fgetc (f)) != ' '
	      || (c = fgetc (f)) != 'y')
	    {
	      if (progress)
		progress->pause_stdout ();
	      fprintf (stderr, "Error parsing cpfind's pto file 2\n");
	      if (progress)
		progress->resume_stdout ();
	      return false;
	    }
	  if (fscanf (f, "%f", &y1) != 1)
	    {
	      if (progress)
		progress->pause_stdout ();
	      fprintf (stderr, "Error parsing cpfind's pto file 3\n");
	      if (progress)
		progress->resume_stdout ();
	      return false;
	    }
	  if ((c = fgetc (f)) != ' '
	      || (c = fgetc (f)) != 'X')
	    {
	      if (progress)
		progress->pause_stdout ();
	      fprintf (stderr, "Error parsing cpfind's pto file 4\n");
	      if (progress)
		progress->resume_stdout ();
	      return false;
	    }
	  if (fscanf (f, "%f", &x2) != 1)
	    {
	      if (progress)
		progress->pause_stdout ();
	      fprintf (stderr, "Error parsing cpfind's pto file 5\n");
	      if (progress)
		progress->resume_stdout ();
	      return false;
	    }
	  if ((c = fgetc (f)) != ' '
	      || (c = fgetc (f)) != 'Y')
	    {
	      if (progress)
		progress->pause_stdout ();
	      fprintf (stderr, "Error parsing cpfind's pto file 6\n");
	      if (progress)
		progress->resume_stdout ();
	      return false;
	    }
	  if (fscanf (f, "%f", &y2) != 1)
	    {
	      if (progress)
		progress->pause_stdout ();
	      fprintf (stderr, "Error parsing cpfind's pto file 7\n");
	      if (progress)
		progress->resume_stdout ();
	      return false;
	    }
#if 0
	  if (fscanf (f, "x%f y%f X%f Y%f to\n", &x1, &y1, &x2, &y2) != 4)
	    {
	      printf (stderr, "Parse error\n");
	      progress->resume_stdout ();
	      return false;
	    }
#endif

	  int xo = round (x2 - x1);
	  int yo = round (y2 - y1);
	  if (direction >= 0)
	    {
	      int xx = -xo / (coord_t)scale - (m_xshift - other.m_xshift);
	      int yy = -yo / (coord_t)scale - (m_yshift - other.m_yshift);
	      coord_t xi, yi;
	      map.to_img (lx + xx, ly + yy, &xi, &yi);

	      if ((direction == 0 && (xi < 0 || fabs (yi) > fabs (xi)/5))
		  || (direction == 1 && (yi < 0 || fabs (xi) > fabs (yi)/5)))
		{
		  fprintf (report_file, "Control point %f %f %f %f identified by cpfind discarded since offset is in wrong direction %f %f\n", x1,y1,x2,y2, xi, yi);
		  continue;
		}
	    }
	  if (fabs ((x2 - x1) - xo) > 0.3
	      || fabs ((y2 - y1) - yo) > 0.3)
	    {
	      if (report_file)
		fprintf (report_file, "Control point %f %f %f %f identified by cpfind discarded since offset is not integer\n",x1,y1,x2,y2);
	      bad_off.push_back ({x2-x1, y2-y1});
	      continue;
	    }
	  /* For Paget screens only shifts with even sum makes sense
	     TODO: If we ever support other scaled screens than pagets, this needs to be conditional.  */
	  if (scale == 2 && ((xo + yo) & 1))
	    {
	      if (report_file)
		fprintf (report_file, "Control point %f %f %f %f identified by cpfind discarded since sum is not even\n",x1,y1,x2,y2);
	      bad_off.push_back ({x2-x1, y2-y1});
	      continue;
	    }
	  if (report_file)
	    fprintf (report_file, "Control point: x1 %f y2 %f x2 %f y2 %f offsetx %i offsety %i\n",x1,y1,x2,y2,xo,yo);
	  bool found = false;
	  for (offsets &o : off)
	    if (o.x == xo && o.y == yo)
	      {
		 o.n++;
		 found = true;
	      }
	  if (!found)
	    off.push_back ((struct offsets){xo,yo,1});
	}
      fclose (f);
      if (off.size ())
	{
	  offsets max;
	  max = off[0];
	  for (offsets &o : off)
	    if (o.n > max.n)
	      max = o;
	  int real_n = max.n;
	  for (bad_offsets &o : bad_off)
	    if (fabs (o.x - max.x) < 0.5 && fabs (o.y - max.y) < 0.5)
	      real_n++;
	  if (real_n > std::min (npoints / 3, 2))
	    {
	      *xshift_ret = -max.x / (coord_t)scale - (m_xshift - other.m_xshift);
	      *yshift_ret = -max.y / (coord_t)scale - (m_yshift - other.m_yshift);
	      if (report_file)
		fprintf (report_file, "Best offset %f %f with %i points, shifts %i %i\n", *xshift_ret, *yshift_ret, max.n, m_xshift - other.m_xshift, m_yshift - other.m_yshift);
	      return true;
	    }
	  else
	    if (report_file)
	      fprintf (report_file, "cpfind result does not seem reliable. Best match is only %i points out of %i\n", max.n, npoints);
	}
      }
  if (report_file)
    fprintf (report_file, "cpfind failed to find useful points; trying to find match myself\n");
  return false;
}

int
analyze_base::find_best_match (int percentage, int max_percentage, analyze_base &other, int cpfind, coord_t *xshift_ret, coord_t *yshift_ret, int direction, scr_to_img &map, scr_to_img &other_map, FILE *report_file, progress_info *progress)
{
  bool val_known = false;
  /* Top left corner of other scan in screen coordinates.  */
  coord_t lx, ly;
  other_map.to_scr (0, 0, &lx, &ly);
  if (cpfind)
    {
      if (find_best_match_using_cpfind (other, xshift_ret, yshift_ret, direction, map, other_map, 1, report_file, progress))
	{
	  val_known = true;
	  if (cpfind != 2)
	    return true;
	}
    }
  int xstart, xend, ystart, yend;
  bool found = false;
  luminosity_t best_sqsum = 0;
  luminosity_t best_rscale, best_gscale, best_bscale;
  int best_xshift = 0, best_yshift = 0;
  int best_noverlap;

  struct range_t {int min, max;};
  std::vector <range_t> range;
  std::vector <range_t> other_range;
  range.resize (m_height);

  int first = -1, last = -1;
  int left = -1, right = -1;
  for (int y = 0; y < m_height; y++)
    {
      int x;
      for (x = 0; x < m_width; x++)
	if (m_known_pixels->test_bit (x, y))
	  break;
      if (x == m_width)
	{
	  range[y].min = 0;
	  range[y].max = -1;
	  continue;
	}
      last = y;
      range[y].min = x;
      for (x = m_width - 1; !m_known_pixels->test_bit (x, y); x--)
	;
      range[y].max = x;
      if (first == -1)
	{
	  first = y;
	  left = range[y].min;
	  right = range[y].max;
	}
      else
	{
	  left = std::min (left, range[y].min);
	  right = std::max (right, range[y].max);
	}
    }
  other_range.resize (other.m_height);
  int other_first = -1, other_last = -1;
  int other_left = -1, other_right = -1;
  for (int y = 0; y < other.m_height; y++)
    {
      int x;
      for (x = 0; x < other.m_width; x++)
	if (other.m_known_pixels->test_bit (x, y))
	  break;
      if (x == other.m_width)
	{
	  other_range[y].min = 0;
	  other_range[y].max = -1;
	  continue;
	}
      other_last = y;
      other_range[y].min = x;
      for (x = other.m_width - 1; !other.m_known_pixels->test_bit (x, y); x--)
	;
      other_range[y].max = x;
      if (other_first == -1)
	{
	  other_first = y;
	  other_left = other_range[y].min;
	  other_right = other_range[y].max;
	}
      else
	{
	  other_left = std::min (other_left, other_range[y].min);
	  other_right = std::max (other_right, other_range[y].max);
	}
    }
  if (first == -1 || other_first == -1)
    {
      if (report_file)
	fprintf (report_file, "Failed to find known pixels\n");
      return false;
    }


  xstart = left - other_right + 2;
  ystart = first - other_last + 2;
  xend = right - other_left - 2;
  yend = last - other_first - 2;

  xstart -= m_xshift - other.m_xshift;
  ystart -= m_yshift - other.m_yshift;
  xend -= m_xshift - other.m_xshift;
  yend -= m_yshift - other.m_yshift;
  if (report_file && val_known && ((*xshift_ret < xstart || *xshift_ret > xend) || (*yshift_ret < ystart || *yshift_ret > yend)))
    fprintf (report_file, "cpfind output: %f,%f out of range\n", *xshift_ret, *yshift_ret);

  rgbdata *sums = (rgbdata *) malloc (sizeof (rgbdata) * m_width * m_height);
  rgbdata *other_sums = (rgbdata *) malloc (sizeof (rgbdata) * other.m_width * other.m_height);
  if (!sums || !other_sums)
    {
      if (progress)
	progress->pause_stdout ();
      printf ("Out of memory allocating density summary\n");
      if (progress)
	progress->resume_stdout ();
      return false;
    }
  if (progress)
    progress->set_task ("summarizing densities", m_height + other.m_height);
  for (int y = 0; y < m_height; y++)
    {
      int x;
      rgbdata sum = {0,0,0};
      for (x = 0; x < m_width; x++)
	{
	  sum.red += red_avg (x, y);
	  sum.green += green_avg (x, y);
	  sum.blue += blue_avg (x, y);
	}
      for (x = 0; x < m_width; x++)
	{
	  sums[y * m_width + x] = sum;
	  sum.red -= red_avg (x, y);
	  sum.green -= green_avg (x, y);
	  sum.blue -= blue_avg (x, y);
	}
      if (progress)
	progress->inc_progress ();
    }
  for (int y = 0; y < other.m_height; y++)
    {
      int x;
      rgbdata sum = {0,0,0};
      for (x = 0; x < other.m_width; x++)
	{
	  sum.red += other.red_avg (x, y);
	  sum.green += other.green_avg (x, y);
	  sum.blue += other.blue_avg (x, y);
	}
      for (x = 0; x < other.m_width; x++)
	{
	  other_sums[y * other.m_width + x] = sum;
	  sum.red -= other.red_avg (x, y);
	  sum.green -= other.green_avg (x, y);
	  sum.blue -= other.blue_avg (x, y);
	}
      if (progress)
	progress->inc_progress ();
    }
  if (progress)
    progress->set_task ("determining best overlap", (yend - ystart));
#pragma omp parallel for default (none) shared (progress, xstart, xend, ystart, yend, other, percentage, max_percentage, found, best_sqsum, best_xshift, best_yshift, best_rscale, best_gscale, best_bscale, best_noverlap, first, last, other_first, other_last, left, right, other_left, other_right, range, other_range, val_known, xshift_ret, yshift_ret, sums, other_sums, report_file,direction,map,lx,ly)
  for (int y = ystart; y < yend; y++)
    {
      bool lfound = false;
      luminosity_t lbest_sqsum = 0;
      int lbest_xshift = 0, lbest_yshift = 0;
      luminosity_t lbest_rscale = 0, lbest_gscale = 0, lbest_bscale = 0;
      int lbest_noverlap;
      for (int x = xstart; x < xend; x++)
	{
	  int est_noverlap = 0;
	  int xxstart = -m_xshift + left;
	  int xxend = -m_xshift + right + 1;
	  int yystart = -m_yshift + first;
	  int yyend = -m_yshift + last + 1;
	  luminosity_t sqsum = 0;
	  bool is_cpfind = false;
	  if (report_file && val_known && *xshift_ret == x && *yshift_ret == y)
	    is_cpfind = true;

#if 1
	  if (direction >= 0)
	    {
	      coord_t xi, yi;
	      map.to_img (x + lx, y + ly, &xi, &yi);
	      if ((direction == 0 && (xi < 0 || fabs (yi) > fabs (xi)/5))
		  || (direction == 1 && (yi < 0 || fabs (xi) > fabs (yi)/5)))
		{
		  if (is_cpfind)
		    fprintf (report_file, "cpfind offset in wrong direction %f %f\n", xi, yi);
		  continue;
		}
	    }
#endif
#if 0
	  if (direction >= 0)
	    {
	      coord_t xi, yi;
	      map.scr_to_final (x, y, &xi, &yi);
	      if (fabs (yi) > fabs (xi)/8
		  &&  (fabs (xi) > fabs (yi)/8))
		{
		  if (is_cpfind)
		    fprintf (report_file, "cpfind offset in wrong direction %f %f\n", xi, yi);
		  continue;
		}
	    }
#endif

	  xxstart = std::max (-other.m_xshift + x + other_left, xxstart);
	  yystart = std::max (-other.m_yshift + y + other_first, yystart);
	  xxend = std::min (-other.m_xshift + other_right + 1 + x, xxend);
	  yyend = std::min (-other.m_yshift + other_last + 1 + y, yyend);

	  //if (yystart >= yyend || xxstart >= xxend)
	    //continue;
	  //printf ("Shift %i %i checking %i to %i, %i to %i; img1 %i %i %i %i; img2 %i %i %i %i\n", x, y, xxstart, xxend, yystart, yyend, m_xshift, m_yshift, m_width, m_height, other.m_xshift, other.m_yshift, other.m_width, other.m_height);
	  assert (yystart < yyend && xxstart < xxend);
	  if ((xxend - xxstart) * (yyend - yystart) * 100 < std::min (m_n_known_pixels, other.m_n_known_pixels) * percentage)
	    {
	      if (is_cpfind)
		fprintf (report_file, "cpfind overlap too small test 1 max:%i known:%i (ranges %i...%i %i...%i)\n", (xxend - xxstart) * (yyend - yystart), std::min (m_n_known_pixels, other.m_n_known_pixels), xxstart, xxend, yystart, yyend);
	      continue;
	    }

#if 1
	  int xstep = std::max ((xxend - xxstart) / 30, 1);
	  int ystep = std::max ((yyend - yystart) / 30, 1);
#else
	  int xstep = 1;
	  int ystep = 1;
#endif
	  luminosity_t rsum1 = 0, rsum2 = 0, gsum1 = 0, gsum2 = 0, bsum1 = 0, bsum2 = 0;

	  //printf ("Shift %i %i checking %i to %i, %i to %i; img1 %i %i %i %i; img2 %i %i %i %i\n", x, y, xxstart, xxend, yystart, yyend, m_xshift, m_yshift, m_width, m_height, other.m_xshift, other.m_yshift, other.m_width, other.m_height);
	  for (int yy = yystart; yy < yyend; yy++)
	    {
	      int y1 = yy + m_yshift;
	      int xxstart = -m_xshift + range[y1].min;
	      int xxend = -m_xshift + range[y1].max + 1;
	      int y2 = yy - y + other.m_yshift;
	      xxstart = std::max (-other.m_xshift + x + other_range[y2].min, xxstart);
	      xxend = std::min (-other.m_xshift + other_range[y2].max + 1 + x, xxend);
	      if (xxend > xxstart)
		{
		  est_noverlap += xxend - xxstart;
		  rsum1 += sums[xxstart + m_xshift + y1 * m_width].red - sums[xxend - 1 + m_xshift + y1 * m_width].red;
		  gsum1 += sums[xxstart + m_xshift + y1 * m_width].green - sums[xxend - 1 + m_xshift + y1 * m_width].green;
		  bsum1 += sums[xxstart + m_xshift + y1 * m_width].blue - sums[xxend - 1 + m_xshift + y1 * m_width].blue;
		  rsum2 += other_sums[xxstart - x + other.m_xshift + y2 * other.m_width].red - other_sums[xxend - x - 1 + other.m_xshift + y2 * other.m_width].red;
		  gsum2 += other_sums[xxstart - x + other.m_xshift + y2 * other.m_width].green - other_sums[xxend - x - 1 + other.m_xshift + y2 * other.m_width].green;
		  bsum2 += other_sums[xxstart - x + other.m_xshift + y2 * other.m_width].blue - other_sums[xxend - x - 1 + other.m_xshift + y2 * other.m_width].blue;
		}
	    }
	  if (est_noverlap * 100 < std::min (m_n_known_pixels, other.m_n_known_pixels) * percentage)
	    {
	      if (is_cpfind)
		fprintf (report_file, "cpfind overlap too small test 2 estimated overlap:%i known %i\n", est_noverlap, std::min (m_n_known_pixels, other.m_n_known_pixels));
	      continue;
	    }
	  if (est_noverlap * 100 > std::max (m_n_known_pixels, other.m_n_known_pixels) * max_percentage)
	    {
	      if (is_cpfind)
		fprintf (report_file, "cpfind overlap too large test 2 estimated overlap:%i known %i\n", est_noverlap, std::min (m_n_known_pixels, other.m_n_known_pixels));
	      continue;
	    }
	  
#if 0
	  int noverlap = 0;
	  for (int yy = yystart; yy < yyend; yy+= ystep)
	    {
	      int y1 = yy + m_yshift;
	      int xxstart = -m_xshift + range[y1].min;
	      int xxend = -m_xshift + range[y1].max + 1;
	      int y2 = yy - y + other.m_yshift;
	      xxstart = std::max (-other.m_xshift + x + other_range[y2].min, xxstart);
	      xxend = std::min (-other.m_xshift + other_range[y2].max + 1 + x, xxend);
	      for (int xx = xxstart; xx < xxend; xx+= xstep)
		{
		  int x1 = xx + m_xshift;
#if 0
		  if (!m_known_pixels->test_bit (x1, y1))
		  {
		    abort ();
		    continue;
		  }
#endif
		  int x2 = xx - x + other.m_xshift;
#if 0
		  if (!other.m_known_pixels->test_bit (x2, y2))
		  {
		    abort ();
		    continue;
		  }
#endif
		  rsum1 += red (2 * x1, y1) + red (2 * x1 + 1, y1);
		  rsum2 += other.red (2 * x2, y2) + other.red (2 * x2 + 1, y2);
		  gsum1 += green (x1, y1);
		  gsum2 += other.green (x2, y2);
		  bsum1 += blue (x1, y1);
		  bsum2 += other.blue (x2, y2);
		  noverlap++;
		}
	    }
	  if (noverlap * xstep * ystep * 100 < std::min (m_n_known_pixels, other.m_n_known_pixels) * percentage)
	    {
	      if (is_cpfind)
		printf ("cpfind overlap too small test 3 overlap:%i steps %i %i known %i\n", noverlap, xstep, ystep, std::min (m_n_known_pixels, other.m_n_known_pixels));
	      continue;
	    }
#endif
	  luminosity_t rscale = rsum1 > 0 ? rsum2 / rsum1 : 1;
	  luminosity_t gscale = gsum1 > 0 ? gsum2 / gsum1 : 1;
	  luminosity_t bscale = bsum1 > 0 ? bsum2 / bsum1 : 1;
	  const luminosity_t exposure_tolerance = 2.6;
	  if (fabs (rscale - 1) > exposure_tolerance
	      || fabs (gscale -1) > exposure_tolerance
	      || fabs (bscale -1) > exposure_tolerance)
	    {
	      if (is_cpfind)
		fprintf (report_file, "cpfind answer rejected because of overall density (red %f:%f %f green %f:%f %f blue %f:%f %f\n", rsum1, rsum2, rscale, gsum1, gsum2, gscale, bsum1, bsum2, bscale);
	      continue;
	    }
	  if (is_cpfind)
	    fprintf (report_file, "cpfind answer exposure correction %f %f %f\n", rscale, gscale, bscale);

	  for (int yy = yystart; yy < yyend; yy+= ystep)
	    {
	      int y1 = yy + m_yshift;
	      int xxstart = -m_xshift + range[y1].min;
	      int xxend = -m_xshift + range[y1].max + 1;
	      int y2 = yy - y + other.m_yshift;
	      xxstart = std::max (-other.m_xshift + x + other_range[y2].min, xxstart);
	      xxend = std::min (-other.m_xshift + other_range[y2].max + 1 + x, xxend);
	      for (int xx = xxstart; xx < xxend; xx+= xstep)
		{
		  int x1 = xx + m_xshift;
		  int y1 = yy + m_yshift;
#if 0
		  if (!m_known_pixels->test_bit (x1, y1))
		    continue;
#endif
		  int x2 = xx - x + other.m_xshift;
		  int y2 = yy - y + other.m_yshift;
#if 0
		  if (!other.m_known_pixels->test_bit (x2, y2))
		    continue;
#endif
		  luminosity_t rdiff1 = red_avg ( x1, y1) * rscale - other.red_avg (x2, y2);
		  luminosity_t gdiff = green_avg (x1, y1) * gscale - other.green_avg (x2, y2);
		  luminosity_t bdiff = blue_avg (x1, y1) * bscale - other.blue_avg (x2, y2);
		  //sqsum += fabs (rdiff1) + fabs (rdiff2) + fabs (gdiff) + fabs (bdiff);
		  rdiff1 *= 65546;
		  gdiff *= 65536;
		  bdiff *= 65536;
		  sqsum += rdiff1*rdiff1 + gdiff*gdiff + bdiff*bdiff;
		  //sqsum += rdiff1*rdiff1*rdiff1*rdiff1 + rdiff2*rdiff2*rdiff2*rdiff2 + gdiff*gdiff*gdiff*gdiff + bdiff*bdiff*bdiff*bdiff;
		}
	    }
	  sqsum /= est_noverlap;
	  if (is_cpfind)
	    fprintf (report_file, "cpfind answer sqsum %f overlap %i\n", sqsum, est_noverlap);
	  if (!lfound || sqsum < lbest_sqsum)
	    {
	      lfound = true;
	      lbest_sqsum = sqsum;
	      lbest_xshift = x;
	      lbest_yshift = y;
	      //lbest_noverlap = noverlap * xstep * ystep;
	      lbest_noverlap = est_noverlap;
	      lbest_rscale = rscale;
	      lbest_gscale = gscale;
	      lbest_bscale = bscale;
	    }
	}
      if (progress)
	progress->inc_progress ();
      if (lfound)
#pragma omp critical
	{
	  if (!found || lbest_sqsum < best_sqsum)
	    {
	      found = 1;
	      best_sqsum = lbest_sqsum;
	      best_xshift = lbest_xshift;
	      best_yshift = lbest_yshift;
	      best_rscale = lbest_rscale;
	      best_gscale = lbest_gscale;
	      best_bscale = lbest_bscale;
	      best_noverlap = lbest_noverlap;
	    }
	}
    }
  free (sums);
  free (other_sums);
  coord_t fx, fy;
  map.scr_to_final (best_xshift, best_yshift, &fx, &fy);
  if (found && report_file)
    fprintf (report_file, "Best match on offset %i,%i (final %f, %f0 sqsum %f scales %f,%f,%f overlap %f%%\n", best_xshift, best_yshift, fx, fy, best_sqsum, best_rscale, best_gscale, best_bscale, 100.0 * best_noverlap / std::min (m_n_known_pixels, other.m_n_known_pixels));
  if (val_known && (*xshift_ret != best_xshift || *yshift_ret != best_yshift))
    {
      if (progress)
	progress->pause_stdout ();
      coord_t fx1, fy1;
      map.scr_to_final (*xshift_ret, *yshift_ret, &fx1, &fy1);
      if (report_file)
        fprintf (report_file, "Mismatch with cpfind: %f,%f (final: %f,%f) compared to %i,%i (final: %f,%f)\n", *xshift_ret, *yshift_ret, fx1, fy1, best_xshift, best_yshift, fx, fy);
      printf ("Mismatch with cpfind: %f,%f (final: %f,%f) compared to %i,%i (final: %f,%f)\n", *xshift_ret, *yshift_ret, fx1, fy1, best_xshift, best_yshift, fx, fy);
      if (progress)
	progress->resume_stdout ();
      return false;
    }
  *xshift_ret = best_xshift;
  *yshift_ret = best_yshift;
  int y = best_yshift;
  int x = best_xshift;
  /* Output heatmap.  */
  if (0)
    {
      static int fn;
      char name[256];
      name[255]=0;
      snprintf (name, 255, "overlap%i%c.tif", fn, direction ? 'v' : 'h');
      unsigned char ov[256][256][3];
      memset (ov, 0, 256*256*3);
      fn++;
      int xxstart = -m_xshift + left;
      int xxend = -m_xshift + right + 1;
      int yystart = -m_yshift + first;
      int yyend = -m_yshift + last + 1;

      xxstart = std::max (-other.m_xshift + x + other_left, xxstart);
      yystart = std::max (-other.m_yshift + y + other_first, yystart);
      xxend = std::min (-other.m_xshift + other_right + 1 + x, xxend);
      yyend = std::min (-other.m_yshift + other_last + 1 + y, yyend);

      for (int yy = yystart; yy < yyend; yy++)
	{
	  int y1 = yy + m_yshift;
	  int xxstart = -m_xshift + range[y1].min;
	  int xxend = -m_xshift + range[y1].max + 1;
	  int y2 = yy - y + other.m_yshift;
	  xxstart = std::max (-other.m_xshift + x + other_range[y2].min, xxstart);
	  xxend = std::min (-other.m_xshift + other_range[y2].max + 1 + x, xxend);
	  for (int xx = xxstart ; xx < xxend; xx++)
	    {
	      int x1 = xx + m_xshift;
	      int x2 = xx - x + other.m_xshift;
	      add (ov, 0,  red_avg (x1, y1), other.red_avg (x2, y2));
	      add (ov, 1,  green_avg (x1, y1), other.green_avg (x2, y2));
	      add (ov, 2,  blue_avg (x1, y1), other.blue_avg (x2, y2));

	      //fprintf (redf, "%f %f\n", red (x1 * 2, y1), other.red (x2 * 2, y2));
	      //fprintf (redf, "%f %f\n", red (x1 * 2+ 1, y1), other.red (x2 * 2+ 1, y2));
	      //fprintf (greenf, "%f %f\n", green (x1, y1), other.green (x2, y2));
	      //fprintf (bluef, "%f %f\n", blue (x1, y1), other.blue (x2, y2));
	    }
	}
      const char *error;
      tiff_writer_params p;
      p.filename = name;
      p.width = 256;
      p.height = 256;
      p.depth = 8;
      tiff_writer out (p, &error);
      if (error)
        {
	  if (progress)
	    progress->pause_stdout ();
	  if (report_file)
	    fprintf (report_file, "Failed to write %s:%s\n", name, error);
          fprintf (stderr, "Failed to write %s:%s\n", name, error);
	  if (progress)
	    progress->resume_stdout ();
	  return false;
        }
      for (int y = 0; y < 256; y++)
	{
	  memcpy (out.row8bit(), ov[y], 256 * 3);
	  if (!out.write_row ())
	    {
	      if (progress)
		progress->pause_stdout ();
	      if (report_file)
		fprintf (report_file, "Failed to write %s (disk full)\n", name);
	      fprintf (stderr, "Failed to write %s (disk full)\n", name);
	      if (progress)
		progress->resume_stdout ();
	      return false;
	    }
	}
    }

  return found ? (val_known || !cpfind ? 2 : 1) : 0;
}
bool
analyze_base::write_screen (const char *filename, bitmap_2d *known_pixels, const char **error, progress_info *progress, luminosity_t rmin, luminosity_t rmax, luminosity_t gmin, luminosity_t gmax, luminosity_t bmin, luminosity_t bmax)
{
  tiff_writer_params p;
  p.filename = filename;
  p.width = m_width;
  p.height = m_height;
  p.alpha = true;
  tiff_writer out(p, error);
  if (*error)
    return false;
  if (progress)
    progress->set_task ("Saving screen", m_height);
  //progress->pause_stdout ();
  //printf ("Saving screen to %s in resolution %ix%i\n", filename, m_width, m_height);
  //progress->resume_stdout ();
  luminosity_t rscale = rmax > rmin ? 1/(rmax-rmin) : 1;
  luminosity_t gscale = gmax > gmin ? 1/(gmax-gmin) : 1;
  luminosity_t bscale = bmax > bmin ? 1/(bmax-bmin) : 1;
  for (int y = 0; y < m_height; y++)
    {
      for (int x = 0; x < m_width; x++)
	{
	  if ((known_pixels ? known_pixels : m_known_pixels)->test_bit (x, y))
	    {
	      out.row16bit ()[x * 4 + 0] = std::max (std::min (linear_to_srgb ((red_avg (x, y) - rmin) * rscale) * 65535.5, 65535.0), 0.0);
	      out.row16bit ()[x * 4 + 1] = std::max (std::min (linear_to_srgb ((green_avg (x, y) - gmin) * gscale) * 65535.5, 65535.0), 0.0);
	      out.row16bit ()[x * 4 + 2] = std::max (std::min (linear_to_srgb ((blue_avg (x, y) - bmin) * bscale) * 65535.5, 65535.0), 0.0);
	      out.row16bit ()[x * 4 + 3] = 65535;
	    }
	  else
	    {
	      out.row16bit ()[x * 4 + 0] = 0;
	      out.row16bit ()[x * 4 + 1] = 0;
	      out.row16bit ()[x * 4 + 2] = 0;
	      out.row16bit ()[x * 4 + 3] = 0;
	    }
	}
      if (!out.write_row ())
	{
	  *error = "Write error";
	  return false;
	}
     if (progress)
       progress->inc_progress ();
    }
  return true;
}

void
analyze_base::analyze_range (luminosity_t *rrmin, luminosity_t *rrmax, luminosity_t *rgmin, luminosity_t *rgmax, luminosity_t *rbmin, luminosity_t *rbmax)
{
  std::vector<luminosity_t> rvec;
  std::vector<luminosity_t> gvec;
  std::vector<luminosity_t> bvec;
  for (int y = 0; y < m_height; y++)
    for (int x = 0; x < m_width; x++)
      if (m_known_pixels->test_bit (x, y))
	{
	  rvec.push_back (red_avg (x, y));
	  gvec.push_back (green_avg (x, y));
	  bvec.push_back (blue_avg (x, y));
	}
  if (!rvec.size ())
  {
    *rrmin = 0;
    *rrmax = 1;
    *rgmin = 0;
    *rgmax = 1;
    *rbmin = 0;
    *rbmax = 1;
    return;
  }
  sort (rvec.begin(), rvec.end ());
  sort (gvec.begin(), gvec.end ());
  sort (bvec.begin(), bvec.end ());
  *rrmin = rvec[rvec.size () * 0.03];
  *rgmin = gvec[gvec.size () * 0.03];
  *rbmin = bvec[bvec.size () * 0.03];
  *rrmax = rvec[ceil ((rvec.size ()-1) * 0.97)];
  *rgmax = gvec[ceil ((gvec.size ()-1) * 0.97)];
  *rbmax = bvec[ceil ((bvec.size ()-1) * 0.97)];
#if 0
  luminosity_t rmin = 1, rmax = 0;
  luminosity_t gmin = 1, gmax = 0;
  luminosity_t bmin = 1, bmax = 0;
  std::

  for (int y = 0; y < m_height; y++)
    for (int x = 0; x < m_width; x++)
      {
	rmin = std::min (rmin, red (x * 2 + 1, y));
	rmax = std::max (rmax, red (x * 2 + 1, y));
	rmin = std::min (rmin, red (x * 2 + 1, y));
	rmax = std::max (rmax, red (x * 2 + 1, y));
	gmin = std::min (gmin, green (x, y));
	gmax = std::max (gmax, green (x, y));
	bmin = std::min (bmin, blue (x, y));
	bmax = std::max (bmax, blue (x, y));
      }
   *rrmin = rmin;
   *rrmax = rmax;
   *rgmin = gmin;
   *rgmax = gmax;
   *rbmin = bmin;
   *rbmax = bmax;
#endif
}
