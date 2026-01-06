#include <time.h>
#include <filesystem>
#include <stdio.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <assert.h>
#include <gtk/gtkbuilder.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <math.h>
#include <cairo.h>
#include <stdbool.h>

#include "config.h"
#include "gtk-image-viewer.h"
#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/screen-map.h"
#include "../libcolorscreen/include/stitch.h"
#include "../libcolorscreen/include/finetune.h"
#include "../libcolorscreen/include/histogram.h"

using namespace colorscreen;

#define UNDOLEVELS 100 
#define PREVIEWSIZE 600
static const bool tmphack = true;
static const char *colorcmd = "montage -geometry 300x300+10+10 /tmp/colororig.tif /tmp/colorsimulated.tif /tmp/colordiff.tif /tmp/colordot-spread.tif /tmp/colorscr.tif /tmp/colorscr-blur.tif /tmp/colorscr-collected.tif -profile sRGB.icc /tmp/color.tif ; display /tmp/color.tif &";
static const char *bwcmd = "montage -geometry 300x300+10+10 /tmp/bworig.tif /tmp/bwsimulated.tif /tmp/bwdiff.tif /tmp/bwdot-spread.tif /tmp/bwscr.tif /tmp/bwscr-blur.tif /tmp/bwscr-collected.tif /tmp/bw.tif ; display /tmp/bw.tif &";

extern "C" {

enum ui_mode
{
  screen_editing,
  screen_detection,
  motor_correction_editing,
  solver_editing,
  color_profiling
} ui_mode;

#define MAX_SOVER_POINTS 10000
static struct solver_parameters current_solver;
static void setvals (void);
static std::shared_ptr <mesh> current_mesh = NULL;
static const char *binary_name;

detected_screen detected;

/* Undo history and the state of UI.  */
static struct scr_to_img_parameters undobuf[UNDOLEVELS];
static struct scr_to_img_parameters current;
static struct scr_detect_parameters undobuf_scr_detect[UNDOLEVELS];
static struct scr_detect_parameters current_scr_detect;
static int undopos;

static bool csp_loaded;

static char *paroname;
static void bigrender (int xoffset, int yoffset, coord_t bigscale, GdkPixbuf * bigpixbuf);

/* The graymap with original scan is stored here.  */
static image_data scan;
static int initialized = 0;
static render_parameters rparams;

/* Status of the main window.  */
static int offsetx = 8, offsety = 8;
static int bigscale = 4;
static bool color_display = true;

static int display_type = 0;
static int scr_detect_display_type = 0;
static bool display_scheduled = true;
static bool preview_display_scheduled = true;
static bool autosolving = false;
static bool finetuning = true;

/* How much is the image scaled in the small view.  */
#define SCALE 16


void
save_parameters (void)
{
  undopos  = (undopos + 1) % UNDOLEVELS;
  undobuf[undopos] = current;
  undobuf_scr_detect[undopos] = current_scr_detect;
}
void
undo_parameters (void)
{
  undopos = (undopos + UNDOLEVELS - 1) % UNDOLEVELS;
  current = undobuf[undopos];
  current_scr_detect = undobuf_scr_detect[undopos];
}
void
redo_parameters (void)
{
  undopos  = (undopos + 1) % UNDOLEVELS;
  current = undobuf[undopos];
  current_scr_detect = undobuf_scr_detect[undopos];
}
void
solve ()
{
  bool mesh = false;
  save_parameters ();
  if (current_mesh)
  {
      current.mesh_trans = NULL;
      mesh = true;
  }
  bool old_optimize_lens = current_solver.optimize_lens;
  bool old_optimize_tilt = current_solver.optimize_tilt;
  if (mesh)
  {
    current_solver.optimize_lens = false;
    //current_solver.optimize_tilt = false;
  }
  coord_t sq;
  {
    file_progress_info progress (stdout);
    sq = solver (&current, scan, current_solver, &progress);
  }
  current_solver.optimize_lens = old_optimize_lens;
  current_solver.optimize_tilt = old_optimize_tilt;
  printf ("Solver %f\n", sq);
  if (mesh)
    {
      file_progress_info progress (stdout);
      current_mesh = solver_mesh (&current, scan, current_solver, &progress);
    }
  setvals ();
  preview_display_scheduled = true;
  display_scheduled = true;
}
void
maybe_solve ()
{
  if (autosolving && current_solver.n_points () >= 3)
    solve ();
}


typedef struct _Data Data;
struct _Data
{
  GtkWidget *save;
  /*GtkImage *bigimage; */
  GtkImage *smallimage;
  GdkPixbuf *bigpixbuf;
  GdkPixbuf *smallpixbuf;
  GtkWidget *maindisplay_scroll;
  GtkWidget *image_viewer;
  GtkSpinButton *gamma, *scanner_mtf_sigma, *presaturation, *saturation, *sharpen_radius, *sharpen_amount, *y2, *brightness, *scanner_snr, *RL_iterations, *sigma,
	       	*tilt_x, *tilt_y, *scanner_mtf_scale, *dark_point, *collection_threshold, *balance_black, *mix_red, *mix_green, *mix_blue,
	       	*balance_red, *balance_green, *balance_blue;
};
Data data;

G_MODULE_EXPORT void cb_press (GtkImage * image, GdkEventButton * event,
			       Data * data);
G_MODULE_EXPORT gboolean
cb_delete_event (GtkWidget * window, GdkEvent * event, Data * data)
{
  gint response = 1;

  /* Run dialog */
  /*response = gtk_dialog_run( GTK_DIALOG( data->quit ) );
     gtk_widget_hide( data->quit ); */

  exit (0);
  return (1 != response);
}

G_MODULE_EXPORT void
cb_show_about (GtkButton * button, Data * data)
{
  /* Run dialog */
  /*gtk_dialog_run( GTK_DIALOG( data->about ) );
     gtk_widget_hide( data->about ); */
}

/* Load the input file. */

static void
openimage (const char *name)
{
  const char *error;
  bool ret;
  {
    file_progress_info p (stdout, true);
    ret = scan.load (name, true, &error, &p, rparams.demosaic);
  }
  if (!ret)
    {
      fprintf (stderr, "%s\n", error);
      exit (1);
    }
#if 0
  current.lens_center.x = scan.width * 0.5;
  current.lens_center.y = scan.height * 0.5;
#endif
  if (!csp_loaded)
    rparams.gamma = scan.gamma != -2 ? scan.gamma : -1;
}

/* Get values displayed in the UI.  */

static void
getvals (void)
{
  render_parameters old = rparams;
  scr_to_img_parameters old2 = current;
  rparams.gamma = gtk_spin_button_get_value (data.gamma);
  rparams.saturation = gtk_spin_button_get_value (data.saturation);
  rparams.presaturation = gtk_spin_button_get_value (data.presaturation);
  rparams.sharpen.usm_radius = gtk_spin_button_get_value (data.sharpen_radius);
  rparams.sharpen.usm_amount = gtk_spin_button_get_value (data.sharpen_amount);
  //rparams.screen_blur_radius = gtk_spin_button_get_value (data.screen_blur);
  luminosity_t sigma = gtk_spin_button_get_value (data.scanner_mtf_sigma);
  if (sigma && !rparams.sharpen.scanner_mtf)
    rparams.sharpen.scanner_mtf = std::make_shared<mtf_parameters> ();
  if (rparams.sharpen.scanner_mtf)
    rparams.sharpen.scanner_mtf->sigma = gtk_spin_button_get_value (data.scanner_mtf_sigma);
  rparams.brightness = gtk_spin_button_get_value (data.brightness);
  current.tilt_x = gtk_spin_button_get_value (data.tilt_x);
  current.tilt_y = gtk_spin_button_get_value (data.tilt_y);
  rparams.sharpen.scanner_mtf_scale = gtk_spin_button_get_value (data.scanner_mtf_scale);
  rparams.dark_point = gtk_spin_button_get_value (data.dark_point);
  rparams.collection_threshold = gtk_spin_button_get_value (data.collection_threshold);
  rparams.backlight_correction_black = gtk_spin_button_get_value (data.balance_black);
  rparams.mix_red = gtk_spin_button_get_value (data.mix_red);
  rparams.mix_green = gtk_spin_button_get_value (data.mix_green);
  rparams.mix_blue = gtk_spin_button_get_value (data.mix_blue);
  rparams.white_balance.red = gtk_spin_button_get_value (data.balance_red);
  rparams.white_balance.green = gtk_spin_button_get_value (data.balance_green);
  rparams.white_balance.blue = gtk_spin_button_get_value (data.balance_blue);
  rparams.sharpen.scanner_snr = gtk_spin_button_get_value (data.scanner_snr);
  rparams.sharpen.richardson_lucy_iterations = gtk_spin_button_get_value (data.RL_iterations);
  rparams.sharpen.richardson_lucy_sigma = gtk_spin_button_get_value (data.sigma);

  if (rparams != old || current != old2)
    {
      maybe_solve ();
      display_scheduled = true;
      preview_display_scheduled = true;
    }
}

/* Set values displayed by the UI.  */

static void
setvals (void)
{
  initialized = 0;
  gtk_spin_button_set_value (data.gamma, rparams.gamma);
  gtk_spin_button_set_value (data.saturation, rparams.saturation);
  gtk_spin_button_set_value (data.presaturation, rparams.presaturation);
  gtk_spin_button_set_value (data.sharpen_radius, rparams.sharpen.usm_radius);
  gtk_spin_button_set_value (data.sharpen_amount, rparams.sharpen.usm_amount);
  gtk_spin_button_set_value (data.scanner_mtf_sigma, 
		  rparams.sharpen.scanner_mtf ? rparams.sharpen.scanner_mtf->sigma : 0);
  gtk_spin_button_set_value (data.brightness, rparams.brightness);
  gtk_spin_button_set_value (data.tilt_x, current.tilt_x);
  gtk_spin_button_set_value (data.tilt_y, current.tilt_y);
  gtk_spin_button_set_value (data.scanner_mtf_scale, rparams.sharpen.scanner_mtf_scale);
  gtk_spin_button_set_value (data.dark_point, rparams.dark_point);
  gtk_spin_button_set_value (data.collection_threshold, rparams.collection_threshold);
  gtk_spin_button_set_value (data.balance_black, rparams.backlight_correction_black);
  gtk_spin_button_set_value (data.mix_red, rparams.mix_red);
  gtk_spin_button_set_value (data.mix_green, rparams.mix_green);
  gtk_spin_button_set_value (data.mix_blue, rparams.mix_blue);
  gtk_spin_button_set_value (data.balance_red, rparams.white_balance.red);
  gtk_spin_button_set_value (data.balance_green, rparams.white_balance.green);
  gtk_spin_button_set_value (data.balance_blue, rparams.white_balance.blue);
  gtk_spin_button_set_value (data.scanner_snr, rparams.sharpen.scanner_snr);
  gtk_spin_button_set_value (data.RL_iterations, rparams.sharpen.richardson_lucy_iterations);
  gtk_spin_button_set_value (data.sigma, rparams.sharpen.richardson_lucy_sigma);
  initialized = 1;
}

static void
print_help()
{
	printf ("\n");
	if (ui_mode == color_profiling)
	   printf ("Color profiling mode\n"
		   "left button adds point, middle/right button removes\n"
		   "p   - switch to screen editing                X   - set mixing weights\n");
	if (ui_mode == screen_detection)
	   printf ("Screen detection mode\n"
		   "e   - switch to screen editing                P   - determine screen proportions\n"
		   "                                              O   - determine screen proportions and assume screen geometry is correct\n"
		   "d   - set black                              r g b- set given color\n");
	if (ui_mode == screen_editing)
	   printf ("Screen editing mode\n"
	           "c   - set center                              C   - set lens center\n"
		   "x   - freeze x                                y   - freeze y                      a - unfreeze both\n"
		   "s S - fast/precise screen collection          O   - optimize colors               P - color profiling mode\n");
	if (ui_mode == solver_editing)
	   printf ("Solver editing mode\n"
	           "w   - switch to screen editing mode\n"
		   "D   - detect regular screen                   a A - autosolve                     L - set lens center\n"
		   "      (finetune mixing in selection)\n"
		   "l   - disable lens center			  dek - remove points in selected region\n"
		   "ctrl+A - autodetect points in selection       ctrl+L - autodetect brightness in selection\n"
		   "f   - finetune screen blur in selection using BW channel\n"
		   "ctrl+f - finetune screen blur in selection using BW channel; with shift using color channel\n"
		   "ctrl+s - finetune strips, screen blur, emulsion blur and fog; record sampling point\n"
		   "alt+s -  finetune strips, emulsion blue, screen blue, fog and do not normaliz using points prevoiusly recorded in ctrl+s\n"
		   "ctrl+w - set white balance so selection is neutral gray\n"
		   "ctrl+d - set mix dark so color in selection\n"
		   "ctrl+m - optimize mixing weights to balance selection to neutral gray\n"
		   "ctrl+i - optimize mixing weights using infrared channel\n");
	if (ui_mode == solver_editing)
	   printf ("Motor editing mode\n"
		   "r   - swithc to screen editing mode\n");
	if (ui_mode == screen_editing || ui_mode == motor_correction_editing || ui_mode == solver_editing)
	   printf ("g G - control film gamma                      d   - set to dufay                  p - set to Paget\n"
	           "f   - set to Finlay                           N   - compute mesh (nonlinear)      n - disable mesh\n"
		   "1-9 - display modes                           t   - scanner type                  R - motor editing mode\n"
		   "l L - dye balance\n");
	else
	   printf ("d   - set dark point                         r g b- set color\n");
	printf    ("W   - switch to solver editing mode           E   - screen editing mode                   \n"
		   "o   - (simulated) infrared/color switch       i   - invert negative             u U - undo / redo\n"
	           "m M - color models                            b B - light temperature      ctrl b B - backlight temperature\n"
		   "q Q - control age                             v V - tone curve                    I - ignore/use infrared\n"
		   "G   - optimize tile adjustments          ctrl G   - reset tile adjustments        w - toggle gammut warning\n");
}

/* Render image into the main window.  */
static void
cb_image_annotate (GtkImageViewer * imgv,
		   GdkPixbuf * pixbuf,
		   gint shift_x,
		   gint shift_y,
		   gdouble scale_x, gdouble scale_y, gpointer user_data)
{
  assert (scale_x == scale_y);
  bigrender (shift_x, shift_y, scale_x, pixbuf);
}

static bool setcenter;
static bool set_lens_center;
static bool set_solver_center;
static bool freeze_x = false;
static bool freeze_y = false;
static void display ();
static int setcolor;
static std::vector<point_t> color_optimizer_points;
static std::vector<color_match> color_optimizer_match;
static double sel1x,sel1y, sel2x,sel2y;

/* Handle all the magic keys.  */
static gint
cb_key_press_event (GtkWidget * widget, GdkEventKey * event)
{
  gint k = event->keyval;

  if (k == 'o')
    {
      color_display = !color_display;
      display_scheduled = true;
    }
  if (k == 'I' && scan.has_grayscale_or_ir () && scan.has_rgb ())
    {
      rparams.ignore_infrared = !rparams.ignore_infrared;
      if (rparams.ignore_infrared)
	printf ("Ignoring infrared\n");
      else
	printf ("Using infrared\n");
      display_scheduled = true;
      preview_display_scheduled = true;
    }
  if (k == 'i' && !(event->state & GDK_CONTROL_MASK))
    {
      rparams.invert = !rparams.invert;
      display_scheduled = true;
      preview_display_scheduled = true;
    }
  if (k == 'J')
    {
      rparams.sharpen.mode = sharpen_parameters::richardson_lucy_deconvolution;
      printf ("Richardson-Lucy Deconvolution\n");
      display_scheduled = true;
      preview_display_scheduled = true;
    }
  if (k == 'j')
    {
      if ((int)rparams.sharpen.mode >= (int)sharpen_parameters::richardson_lucy_deconvolution - 1)
	rparams.sharpen.mode = sharpen_parameters::none;
      else
	rparams.sharpen.mode = (sharpen_parameters::sharpen_mode)((int)rparams.sharpen.mode + 1);
      printf ("Sharpen algorithm: %s\n", sharpen_parameters::sharpen_mode_names [(int)rparams.sharpen.mode]);
      display_scheduled = true;
      preview_display_scheduled = true;
    }
  if (k == '?')
    print_help ();
  if (k == 'u')
    {
      undo_parameters ();	
      setvals ();
      maybe_solve ();
      display_scheduled = true;
      preview_display_scheduled = true;
    }
  if (k == 'U')
    {
      redo_parameters ();
      setvals ();
      maybe_solve ();
      display_scheduled = true;
      preview_display_scheduled = true;
    }
  if ((k == 'm' || k == 'M') && !(event->state & GDK_CONTROL_MASK))
    {
      if (k == 'm')
	{
	  rparams.color_model = (render_parameters::color_model_t)((int)rparams.color_model + 1);
	  if ((int)rparams.color_model >= (int)render_parameters::color_model_max)
	    rparams.color_model = (render_parameters::color_model_t)0;
	}
      else
	{
	  if ((int)rparams.color_model == 0)
	    rparams.color_model = (render_parameters::color_model_t)((int)render_parameters::color_model_max - 1);
	  else
	    rparams.color_model = (render_parameters::color_model_t)((int)rparams.color_model - 1);
	}
      printf ("Color model: %s\n", render_parameters::color_model_names[(int)rparams.color_model]);
      display_scheduled = true;
      preview_display_scheduled = true;
    }
  if (k == 'v' || k == 'V')
    {
      if (k == 'v')
	{
	  rparams.output_tone_curve = (tone_curve::tone_curves)((int)rparams.output_tone_curve + 1);
	  if ((int)rparams.output_tone_curve >= tone_curve::tone_curve_max)
	    rparams.output_tone_curve = (tone_curve::tone_curves)0;
	}
      else
	{
	  if ((int)rparams.output_tone_curve == 0)
	    rparams.output_tone_curve = (tone_curve::tone_curves)((int)tone_curve::tone_curve_max - 1);
	  else
	    rparams.output_tone_curve = (tone_curve::tone_curves)((int)rparams.output_tone_curve - 1);
	}
      printf ("Tone curve: %s\n", tone_curve::tone_curve_names[(int)rparams.output_tone_curve]);
      display_scheduled = true;
      preview_display_scheduled = true;
    }
  if (k == 'E' && scan.has_rgb ())
    {
      ui_mode = screen_detection;
      print_help ();
      printf ("Screen detection mode\n");
      display_scheduled = true;
      preview_display_scheduled = true;
    }
  if (k == 'e' && ui_mode == screen_detection)
    {
      ui_mode = screen_editing;
      print_help ();
      printf ("Screen editing mode\n");
      display_scheduled = true;
      preview_display_scheduled = true;
    }
  if (k == 'w')
    {
      display_scheduled = true;
      rparams.gammut_warning = !rparams.gammut_warning;
      printf ("Gammut warning %s\n", rparams.gammut_warning ? "enabled" : "disabled");
    }
  if (k == 'W')
    {
      display_scheduled = true;
      ui_mode = solver_editing;
      print_help ();
      printf ("Solver editing mode entered\n");
    }
  if (k == 'w' && !(event->state & GDK_CONTROL_MASK) && ui_mode == solver_editing)
    {
      ui_mode = screen_editing;
      print_help ();
      printf ("Screen editing mode\n");
      display_scheduled = true;
    }
  if (k == 'e' && ui_mode == screen_detection)
    {
      ui_mode = screen_editing;
      display_scheduled = true;
      preview_display_scheduled = true;
      print_help ();
      printf ("Screen editing mode\n");
    }
  if (k == ' ')
    {
      gdouble scale_x, scale_y;
      gint shift_x, shift_y;
      gint pxsize =  /*gtk_image_viewer_get_image_width (GTK_IMAGE_VIEWER (data.image_viewer))*/1024;
      gint pysize =  /*gtk_image_viewer_get_image_height (GTK_IMAGE_VIEWER (data.image_viewer))*/800;
      gtk_image_viewer_get_scale_and_shift (GTK_IMAGE_VIEWER (data.image_viewer),
					    &scale_x, &scale_y, &shift_x,
					    &shift_y);
      int minx = std::max ((int)(shift_x / scale_x), 0);
      int maxx = std::min ((int)((shift_x + pxsize) / scale_x), scan.width);
      int miny = std::max ((int)(shift_y / scale_y), 0);
      int maxy = std::min ((int)((shift_y + pysize) / scale_y), scan.height);
      /* We no longer have way to access raw graydata.  */
#if 0
      printf ("%i %i %i %i\n",pxsize, pysize,minx, maxx);
      if (!scan.stitch)
      if (minx < maxx && miny < maxy)
	{
	  bool inverted = rparams.gray_min > rparams.gray_max;
	  rparams.gray_min = scan.maxval;
	  rparams.gray_max = 0;
	  render_img render (current, scan, rparams, 255);
	  render.precompute_all (NULL);
	  for (int y = std::max ((int)(shift_y / scale_y), 0);
	       y < std::min ((int)((shift_y + pysize) / scale_y), scan.height); y++)
	     for (int x = minx; x < maxx; x++)
	       {
		 int pixel = render.render_raw_pixel (x, y);
		 rparams.gray_min = std::min (pixel, rparams.gray_min);
		 rparams.gray_max = std::max (pixel, rparams.gray_max);
	       }
	  display_scheduled = true;
          preview_display_scheduled = true;
	  if (inverted)
	    std::swap (rparams.gray_min, rparams.gray_max);
	}
#endif
     }
  if (k == 'b' && (event->state & GDK_CONTROL_MASK))
    {
      rparams.backlight_temperature = std::max (rparams.backlight_temperature - 100 , (luminosity_t)render_parameters::temperature_min);
      printf ("backlight temperature %f\n",rparams.backlight_temperature);
      display_scheduled = true;
      preview_display_scheduled = true;
    }
  else if (k == 'b')
    {
      rparams.temperature = std::max (rparams.temperature - 100 , (luminosity_t)render_parameters::temperature_min);
      printf ("temperature %f\n",rparams.temperature);
      display_scheduled = true;
      preview_display_scheduled = true;
    }
  if (k == 'B' && (event->state & GDK_CONTROL_MASK))
    {
      rparams.backlight_temperature = std::min (rparams.backlight_temperature + 100 , (luminosity_t)render_parameters::temperature_max);
      printf ("backlight temperature %f\n",rparams.backlight_temperature);
      display_scheduled = true;
      preview_display_scheduled = true;
    }
  else if (k == 'B')
    {
      rparams.temperature = std::max (rparams.temperature + 100 , (luminosity_t)render_parameters::temperature_min);
      printf ("temperature %f\n",rparams.temperature);
      display_scheduled = true;
      preview_display_scheduled = true;
    }
  if (k == 'q' && rparams.age > 0)
    {
      rparams.age-=0.1;
      if (rparams.age < 0)
	rparams.age = 0;
      printf ("Age: %f\n", rparams.age);
      display_scheduled = true;
      preview_display_scheduled = true;
    }
  if (k == 'Q')
    {
      rparams.age+=0.1;
      printf ("Age: %f\n", rparams.age);
      display_scheduled = true;
      preview_display_scheduled = true;
    }
  if (k == 'G' && scan.stitch && (event->state & GDK_CONTROL_MASK))
    {
      rparams.set_tile_adjustments_dimensions (0, 0);
      display_scheduled = true;
      preview_display_scheduled = true;
    }
  else if (k == 'G' && scan.stitch)
    {
      file_progress_info progress (stdout);
      const char *error;
      if (!scan.stitch->optimize_tile_adjustments (&rparams,
			      stitch_project::OPTIMIZE_ALL /*& (~stitch_project::OPTIMIZE_EXPOSURE)*/
			      & ((event->state & GDK_MOD1_MASK) ? ~stitch_project::OPTIMIZE_BACKLIGHT_BLACK : ~0),
			      &error, &progress))
	fprintf (stderr, "exposure analysis failed: %s\n", error);
      setvals ();
      display_scheduled = true;
      preview_display_scheduled = true;
    }
  if (ui_mode == screen_editing)
    {
      if (k == 'c')
	setcenter = true;
      if (k == 'C')
	set_lens_center = true;
      if (k == 'x')
	{
	  freeze_x = false;
	  freeze_y = true;
	}
      if (k == 'y')
	{
	  freeze_x = true;
	  freeze_y = false;
	}
      if (k == 'a')
	{
	  freeze_x = false;
	  freeze_y = false;
	}
      if (k == 's' && !(event->state & GDK_CONTROL_MASK))
	{
	  rparams.collection_quality = render_parameters::fast_collection;
	  printf ("Fast data collection\n");
	  display_scheduled = true;
	}
      if (k == 'S')
	{
	  if (rparams.collection_quality == render_parameters::fast_collection)
	    {
	      rparams.collection_quality = render_parameters::simple_screen_collection;
	      printf ("Simple screen data collection\n");
	    }
	  else
	    {
	      rparams.collection_quality = render_parameters::simulated_screen_collection;
	      printf ("Simulated screen data collection\n");
	    }
	  display_scheduled = true;
	}
      if (k == 'l' && !(event->state & GDK_CONTROL_MASK))
        {
	  if (rparams.dye_balance == render_parameters::dye_balance_none)
	    rparams.dye_balance = (render_parameters::dye_balance_t)((int)render_parameters::dye_balance_max - 1);
	  else
	    rparams.dye_balance = (render_parameters::dye_balance_t)((int)rparams.dye_balance - 1);
	  printf ("Dye balance:%s\n", render_parameters::dye_balance_names [(int)rparams.dye_balance]);
	  display_scheduled = true;
	  preview_display_scheduled = true;
        }
      if (k == 'L' && !(event->state & GDK_CONTROL_MASK))
        {
	  rparams.dye_balance = (render_parameters::dye_balance_t)((int)rparams.dye_balance + 1);
	  if (rparams.dye_balance == render_parameters::dye_balance_max)
	    rparams.dye_balance = render_parameters::dye_balance_none;
	  printf ("Dye balance:%s\n", render_parameters::dye_balance_names [(int)rparams.dye_balance]);
	  display_scheduled = true;
	  preview_display_scheduled = true;
        }
    }
  if (ui_mode == solver_editing)
    {
      if ((k == GDK_KEY_Delete || k == GDK_KEY_BackSpace) && !(event->state & GDK_CONTROL_MASK))
        {
 	  printf ("Deleting %f %f %f %f\n", sel1x, sel1y, sel2x, sel2y);
	  for (int n = 0; n < current_solver.n_points ();)
	    if (current_solver.points[n].img.x > sel1x && current_solver.points[n].img.x < sel2x
	        && current_solver.points[n].img.y > sel1y && current_solver.points[n].img.y < sel2y)
	      current_solver.remove_point (n);
	    else
	      n++;
	  maybe_solve ();
	  display_scheduled = true;
        }
      if ((k == GDK_KEY_Delete || k == GDK_KEY_BackSpace) && (event->state & GDK_CONTROL_MASK))
        {
	  histogram hist;
	  scr_to_img map;
	  map.set_parameters (current, scan);
	  for (int n = 0; n < current_solver.n_points ();n++)
	    if (current_solver.points[n].img.x > sel1x && current_solver.points[n].img.x < sel2x
	        && current_solver.points[n].img.y > sel1y && current_solver.points[n].img.y < sel2y)
	      {
		if (!screen_with_vertical_strips_p (current.type))
		  {
		    point_t p = map.to_img (current_solver.points[n].scr);
		    coord_t dist = p.dist_from (current_solver.points[n].img);
		    hist.pre_account (dist);
		  }
		else
		  {
		    point_t p = map.to_scr (current_solver.points[n].img);
		    coord_t dist = fabs (p.x - current_solver.points[n].scr.x);
		    hist.pre_account (dist);
		  }
	      }
	  hist.finalize_range (65536);
	  for (int n = 0; n < current_solver.n_points ();n++)
	    if (current_solver.points[n].img.x > sel1x && current_solver.points[n].img.x < sel2x
	        && current_solver.points[n].img.y > sel1y && current_solver.points[n].img.y < sel2y)
	      {
		if (!screen_with_vertical_strips_p (current.type))
		  {
		    point_t p = map.to_img (current_solver.points[n].scr);
		    coord_t dist = p.dist_from (current_solver.points[n].img);
		    hist.account (dist);
		  }
		else
		  {
		    point_t p = map.to_scr (current_solver.points[n].img);
		    coord_t dist = fabs (p.x - current_solver.points[n].scr.x);
		    hist.account (dist);
		  }
	      }
	  hist.finalize ();
	  coord_t thresholt = hist.find_max (0.1);
	  for (int n = 0; n < current_solver.n_points ();)
	    {
	      coord_t dist;
	      if (!screen_with_vertical_strips_p (current.type))
		{
		  point_t p = map.to_img (current_solver.points[n].scr);
		  dist = p.dist_from (current_solver.points[n].img);
		}
	      else
		{
		  point_t p = map.to_scr (current_solver.points[n].img);
		  dist = fabs (p.x - current_solver.points[n].scr.x);
		}
	      if (current_solver.points[n].img.x > sel1x && current_solver.points[n].img.x < sel2x
		  && current_solver.points[n].img.y > sel1y && current_solver.points[n].img.y < sel2y
		  && dist > thresholt)
		current_solver.remove_point (n);
	      else
		n++;
	    }
	  maybe_solve ();
	  display_scheduled = true;
        }
      if (k == 'i' && (event->state & GDK_CONTROL_MASK))
        {
	  file_progress_info progress (stdout);
	  if (rparams.auto_mix_weights_using_ir (scan, current, sel1x, sel1y, sel2x, sel2y, &progress))
	    {
	      display_scheduled = true;
	      setvals ();
	    }
        }
      if (k == 'D' && scan.rgbdata)
	{
	  save_parameters ();
	  file_progress_info progress (stdout);

	  if (detected.smap)
	    delete detected.smap;
	  detect_regular_screen_params dsparams;
	  dsparams.return_screen_map = true;
	  dsparams.gamma = rparams.gamma;
	  dsparams.scr_type = current.type;
	  dsparams.scanner_type = current.scanner_type;
	  detected = detect_regular_screen (scan, current_scr_detect, current_solver, &dsparams, &progress);
	  if (detected.success)
	    {
	      int xmin = std::min (sel1x, sel2x);
	      int ymin = std::min (sel1y, sel2y);
	      int xmax = std::max (sel1x, sel2x);
	      int ymax = std::max (sel1y, sel2y);
	      current.type = detected.param.type;
	      current_mesh = detected.mesh_trans;
	      current.mesh_trans = current_mesh;
	      if (rparams.color_model == render_parameters::color_model_none)
		rparams.auto_color_model (current.type);
	      if (!scan.data || rparams.ignore_infrared)
	        {
		  rparams.auto_mix_weights (scan, current, xmin, ymin, xmax, ymax, &progress);
		  setvals ();
	        }
	      //if (rparams.dark_point == 0 && rparams.brightness == 1)
		{
		  rparams.auto_dark_brightness (scan, current, scan.width / 10, scan.height / 10, 9 * scan.width / 10, 9 * scan.height / 10, &progress);
		  setvals ();
		}
	      if (!display_type)
		display_type = (int)render_type_interpolated;
	    }
	  display_scheduled = true;
	  preview_display_scheduled = true;

	}
      if (k == 'l' && (event->state & GDK_CONTROL_MASK))
        {
	  int xmin = std::min (sel1x, sel2x);
	  int ymin = std::min (sel1y, sel2y);
	  int xmax = std::max (sel1x, sel2x);
	  int ymax = std::max (sel1y, sel2y);
	  bool ret;
	  printf ("Auto levels in selection in selection %i %i %i %i old darkpoint %f old brightness %f\n",xmin,ymin,xmax,ymax, rparams.dark_point, rparams.brightness);
	    {
	      file_progress_info progress (stdout);
	      ret = rparams.auto_dark_brightness (scan, current, xmin, ymin, xmax, ymax, &progress);
	    }
	  if (ret)
	    {
	      printf ("New darkpoint %f; new brightness %f\n", rparams.dark_point, rparams.brightness);
	      setvals ();
	      display_scheduled = true;
	      preview_display_scheduled = true;
	    }
	  else
	    printf ("Analysis failed\n");
        }
      if (k == 'm' && (event->state & GDK_CONTROL_MASK))
        {
	  int xmin = std::min (sel1x, sel2x);
	  int ymin = std::min (sel1y, sel2y);
	  int xmax = std::max (sel1x, sel2x);
	  int ymax = std::max (sel1y, sel2y);
	  printf ("Auto mix weights in selection %i %i %i %i\n",xmin,ymin,xmax,ymax);
	  file_progress_info progress (stdout);
	  rparams.auto_mix_weights (scan, current, xmin, ymin, xmax, ymax, &progress);
	  setvals ();
	  display_scheduled = true;
	  preview_display_scheduled = true;
        }
      if (k == 'd' && (event->state & GDK_CONTROL_MASK))
        {
	  int xmin = std::min (sel1x, sel2x);
	  int ymin = std::min (sel1y, sel2y);
	  int xmax = std::max (sel1x, sel2x);
	  int ymax = std::max (sel1y, sel2y);
	  printf ("Auto mix dark in selection %i %i %i %i\n",xmin,ymin,xmax,ymax);
	  file_progress_info progress (stdout);
	  rparams.auto_mix_dark (scan, current, xmin, ymin, xmax, ymax, &progress);
	  setvals ();
	  display_scheduled = true;
	  preview_display_scheduled = true;
        }
      if (k == 'a' && !(event->state & GDK_CONTROL_MASK))
	autosolving = false;
      if (k == 'a' && (event->state & GDK_CONTROL_MASK))
        {
	  int xmin = std::min (sel1x, sel2x);
	  int ymin = std::min (sel1y, sel2y);
	  int xmax = std::max (sel1x, sel2x);
	  int ymax = std::max (sel1y, sel2y);
	  file_progress_info progress (stdout);

	  if (xmin == xmax && ymin == ymax)
	    {
	      xmin = 0;
	      ymin = 0;
	      xmax = scan.width;
	      ymax = scan.height;
	    }
	  if (!scan.stitch && finetune_area (&current_solver, rparams, current, scan, xmin, ymin, xmax, ymax, &progress))
	    {
	      autosolving = true;
	      display_scheduled = true;
	    }
	}
      if (k == 'f' && (event->state & GDK_MOD1_MASK))
      {
	int x = (sel1x + sel2x)/2;
	int y = (sel1y + sel2y)/2;
	printf ("Finetuning focus on %i %i\n",x,y);
	finetune_parameters fparam;
	if (tmphack)
	  {
	    fparam.simulated_file = "/tmp/bwsimulated.tif";
	    fparam.orig_file = "/tmp/bworig.tif";
	    fparam.diff_file = "/tmp/bwdiff.tif";
	    fparam.screen_file = "/tmp/bwscr.tif";
	    fparam.screen_blur_file = "/tmp/bwscr-blur.tif";
	    fparam.collected_file = "/tmp/bwscr-collected.tif";
	    fparam.dot_spread_file = "/tmp/bwdot-spread.tif";
	  }
	fparam.multitile = 3;
	fparam.range = 4;
	fparam.flags |= finetune_position | finetune_bw | finetune_verbose | finetune_emulsion_blur /*| finetune_dufay_strips | finetune_fog*/;
	file_progress_info progress (stdout);
	finetune_result res = finetune (rparams, current, scan, {{(coord_t)x, (coord_t)y}}, NULL, fparam, &progress);
	if (res.success)
	  {
	    rparams.screen_blur_radius = res.screen_blur_radius;
	    display_scheduled = true;
	    if (tmphack)
	      system(bwcmd);
	    setvals ();
	  }
      }
      if (k == 'f' && (event->state & GDK_CONTROL_MASK))
      {
	int x = (sel1x + sel2x)/2;
	int y = (sel1y + sel2y)/2;
	printf ("Finetuning focus on %i %i\n",x,y);
	finetune_parameters fparam;
	if (tmphack)
	  {
	    fparam.simulated_file = "/tmp/bwsimulated.tif";
	    fparam.orig_file = "/tmp/bworig.tif";
	    fparam.diff_file = "/tmp/bwdiff.tif";
	    fparam.screen_file = "/tmp/bwscr.tif";
	    fparam.screen_blur_file = "/tmp/bwscr-blur.tif";
	    fparam.collected_file = "/tmp/bwscr-collected.tif";
	    fparam.dot_spread_file = "/tmp/bwdot-spread.tif";
	  }
	fparam.multitile = 3;
	fparam.range = 4;
	fparam.flags |= finetune_position | finetune_bw | finetune_verbose | /*finetune_screen_blur*/ finetune_scanner_mtf_sigma /*| finetune_dufay_strips | finetune_fog*/;
	file_progress_info progress (stdout);
	finetune_result res = finetune (rparams, current, scan, {{(coord_t)x, (coord_t)y}}, NULL, fparam, &progress);
	if (res.success)
	  {
	    rparams.screen_blur_radius = res.screen_blur_radius;
	    display_scheduled = true;
	    if (tmphack)
	      system(bwcmd);
	    setvals ();
	  }
      }
      if (k == 'F' && (event->state & GDK_CONTROL_MASK))
      {
	int x = (sel1x + sel2x)/2;
	int y = (sel1y + sel2y)/2;
	printf ("Finetuning focus on %i %i\n",x,y);
	finetune_parameters fparam;
	if (tmphack)
	  {
	    fparam.simulated_file = "/tmp/colorsimulated.tif";
	    fparam.orig_file = "/tmp/colororig.tif";
	    fparam.diff_file = "/tmp/colordiff.tif";
	    fparam.screen_file = "/tmp/colorscr.tif";
	    fparam.screen_blur_file = "/tmp/colorscr-blur.tif";
	    fparam.collected_file = "/tmp/colorscr-collected.tif";
	    fparam.dot_spread_file = "/tmp/colordot-spread.tif";
	  }
	fparam.multitile = 3;
	fparam.flags |= finetune_position | finetune_verbose | finetune_screen_blur | (current.type != Joly ? finetune_strips : 0) | finetune_fog;
	file_progress_info progress (stdout);
	finetune_result res = finetune (rparams, current, scan, {{(coord_t)x, (coord_t)y}}, NULL, fparam, &progress);
	if (res.success)
	  {
	    rparams.screen_blur_radius = res.screen_blur_radius;
	    display_scheduled = true;
	    if (tmphack)
	      system(colorcmd);
	    setvals ();
	  }
      }
      static std::vector <point_t> tune_points;
      static std::vector <finetune_result> tune_results;
      if (k == 'w' && (event->state & GDK_CONTROL_MASK))
	{
	  file_progress_info progress (stdout);
	  printf ("Auto white balance in selection\n");
	  if (rparams.auto_white_balance (scan, current, sel1x, sel1y, sel2x, sel2y, &progress))
	    {
	      display_scheduled = true;
	      setvals ();
	    }
	}
      if (k == 's' && (event->state & GDK_CONTROL_MASK))
      {
	int x = (sel1x + sel2x)/2;
	int y = (sel1y + sel2y)/2;
	printf ("Finetuning focus on %i %i\n",x,y);
	finetune_parameters fparam;
	if (tmphack)
	  {
	    fparam.simulated_file = "/tmp/colorsimulated.tif";
	    fparam.orig_file = "/tmp/colororig.tif";
	    fparam.sharpened_file = "/tmp/colorsharpened.tif";
	    fparam.diff_file = "/tmp/colordiff.tif";
	    fparam.screen_file = "/tmp/colorscr.tif";
	    fparam.screen_blur_file = "/tmp/colorscr-blur.tif";
	    fparam.emulsion_file = "/tmp/colorscr-emulsion.tif";
	    fparam.merged_file = "/tmp/colorscr-merged.tif";
	    fparam.collected_file = "/tmp/colorscr-collected.tif";
	    fparam.dot_spread_file = "/tmp/colordot-spread.tif";
	  }
	//fparam.multitile = 3;
	//fparam.flags |= finetune_position | finetune_verbose /*| finetune_screen_mtf_blur*/ | finetune_emulsion_blur /*| finetune_screen_channel_blurs*/ | finetune_screen_blur | finetune_dufay_strips | finetune_fog | finetune_no_normalize;
	fparam.flags |= finetune_position | finetune_verbose /*| finetune_screen_channel_blurs*/ | finetune_scanner_mtf_sigma | (current.type != Joly ? finetune_strips : 0) | finetune_fog | finetune_no_normalize | finetune_simulate_infrared /*| finetune_sharpening*/;
	//fparam.range = 4;
	fparam.range = 8;
	//fparam.ignore_outliers=0.001;
	file_progress_info progress (stdout);
	finetune_result res = finetune (rparams, current, scan, {{(coord_t)x, (coord_t)y}}, NULL, fparam, &progress);
	tune_points.push_back ({(coord_t)x, (coord_t)y});
	tune_results.push_back (res);
	if (res.success)
	  {
	    rparams.screen_blur_radius = res.screen_blur_radius;
	    rparams.red_strip_width = res.red_strip_width;
	    rparams.green_strip_width = res.green_strip_width;
	    rparams.mix_red = res.mix_weights.red;
	    rparams.mix_green = res.mix_weights.green;
	    rparams.mix_blue = res.mix_weights.blue;
	    rparams.mix_dark = res.mix_dark;
	    preview_display_scheduled = true;
	    display_scheduled = true;
	    if (tmphack)
	      system(colorcmd);
	    setvals ();
	  }
      }
      if (k == 's' && (event->state & GDK_MOD1_MASK))
      {
	int x = (sel1x + sel2x)/2;
	int y = (sel1y + sel2y)/2;
	printf ("Finetuning focus on %i %i\n",x,y);
	finetune_parameters fparam;
	if (tmphack)
	  {
	    fparam.simulated_file = "/tmp/colorsimulated.tif";
	    fparam.orig_file = "/tmp/colororig.tif";
	    fparam.diff_file = "/tmp/colordiff.tif";
	    fparam.screen_file = "/tmp/colorscr.tif";
	    fparam.screen_blur_file = "/tmp/colorscr-blur.tif";
	    fparam.collected_file = "/tmp/colorscr-collected.tif";
	    fparam.dot_spread_file = "/tmp/colordot-spread.tif";
	  }
	fparam.multitile = 3;
	fparam.flags |= finetune_position | finetune_verbose /*| finetune_screen_mtf_blur*/ | finetune_emulsion_blur /*| finetune_screen_channel_blurs*/ | finetune_screen_blur | (current.type != Joly ? finetune_strips : 0) | finetune_fog | finetune_no_normalize;
	fparam.range = 4;
	file_progress_info progress (stdout);
	finetune_result res = finetune (rparams, current, scan, tune_points, &tune_results, fparam, &progress);
	if (res.success)
	  {
	    rparams.screen_blur_radius = res.screen_blur_radius;
	    rparams.red_strip_width = res.red_strip_width;
	    rparams.green_strip_width = res.green_strip_width;
	    rparams.mix_red = res.mix_weights.red;
	    rparams.mix_green = res.mix_weights.green;
	    rparams.mix_blue = res.mix_weights.blue;
	    rparams.mix_dark = res.mix_dark;
	    preview_display_scheduled = true;
	    display_scheduled = true;
	    if (tmphack)
	      system(colorcmd);
	    setvals ();
	  }
      }
      if (k == 'A')
      {
	autosolving = true;
	maybe_solve ();
      }
      if (k == 'L' && !!(event->state & GDK_CONTROL_MASK))
	set_solver_center = true;
      if (k == 'l' && !(event->state & GDK_CONTROL_MASK))
      {
	current_solver.weighted = false;
	file_progress_info progress (stdout);
	if (current_mesh)
	  {
	      current.mesh_trans = NULL;
	      current_mesh = NULL;
	  }
	solver (&current, scan, current_solver, &progress);
	preview_display_scheduled = true;
	display_scheduled = true;
      }
    }
  if (ui_mode == color_profiling && k == 'p')
  {
    ui_mode = screen_editing;
    display_scheduled = true;
    print_help ();
    printf ("Color editing mode\n");
    return false;
  }
  if (ui_mode == screen_editing || ui_mode == motor_correction_editing || ui_mode == solver_editing || ui_mode == color_profiling)
    {
      if (k == 'g')
      {
	if (!(event->state & GDK_CONTROL_MASK))
	  {
	    rparams.film_gamma -= 0.01;
	    printf ("Film gamma %f\n", rparams.film_gamma);
	  }
	else
	  {
	    rparams.target_film_gamma -= 0.01;
	    printf ("Target film gamma %f\n", rparams.target_film_gamma);
	  }
	display_scheduled = true;
      }
      if (k == 'G')
      {
	if (!(event->state & GDK_CONTROL_MASK))
	  {
	    rparams.film_gamma += 0.01;
	    printf ("Film gamma %f\n", rparams.film_gamma);
	  }
	else
	  {
	    rparams.target_film_gamma += 0.01;
	    printf ("Target film gamma %f\n", rparams.target_film_gamma);
	  }
	display_scheduled = true;
      }
      if (k == 'd' && current.type != Dufay  && !(event->state & GDK_CONTROL_MASK))
      {
	save_parameters ();
	current.type = Dufay;
	display_scheduled = true;
	preview_display_scheduled = true;
      }
      if (k == 'p' && current.type != Paget)
      {
	save_parameters ();
	current.type = Paget;
	display_scheduled = true;
	preview_display_scheduled = true;
      }
      if (k == 'f' && current.type != Finlay && !(event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK)))
      {
	save_parameters ();
	current.type = Finlay;
	display_scheduled = true;
	preview_display_scheduled = true;
      }
      if (k == 'N')
      {
	current.mesh_trans = NULL;
	file_progress_info progress (stdout);
	current_mesh = solver_mesh (&current, scan, current_solver, &progress);
	display_scheduled = true;
	preview_display_scheduled = true;
      }
      if (k == 'n')
      {
	if (current_mesh)
	  {
	    current.mesh_trans = NULL;
	    current_mesh = NULL;
	    display_scheduled = true;
	    preview_display_scheduled = true;
	  }
      }
      if ((event->state & GDK_CONTROL_MASK)&& scan.stitch && k >= '1' && k <= '9')
        {
	  int idx = k - '1';
	  int y = idx / scan.stitch->params.width;
	  int x = idx % scan.stitch->params.width;
	  if (rparams.tile_adjustments_width != scan.stitch->params.width
	      || rparams.tile_adjustments_height != scan.stitch->params.height)
		  rparams.set_tile_adjustments_dimensions (scan.stitch->params.width, scan.stitch->params.height);
	  if (y < scan.stitch->params.height)
	    {
		  rparams.get_tile_adjustment (x,y).enabled ^= 1;
		  display_scheduled = true;
	    }
        }
      else if (k >= '1' && k <'1' + (int)render_type_first_scr_detect)
	{
	  display_type = k - '1';
	  display_scheduled = true;
	}
      if (k == 't')
	{
	  save_parameters ();
	  current.scanner_type = (scanner_type)((int)current.scanner_type + 1);
	  preview_display_scheduled = true;
	  display_scheduled = true;
	  if (current.scanner_type == max_scanner_type)
	    current.scanner_type = fixed_lens;
	  printf ("scanner type: %s\n", scanner_type_names [(int)current.scanner_type]);
	  maybe_solve ();
	}
      if (k == 'r' && ui_mode == motor_correction_editing)
      {
	ui_mode = screen_editing;
        print_help ();
        printf ("Motor correction mode\n");
      }
      if (k == 'R' && (ui_mode == screen_editing || ui_mode == solver_editing))
	{
	  ui_mode = motor_correction_editing;
          print_help ();
	  printf ("Screen editing mode\n");
	}
      if (k == 'P' && scan.has_rgb ())
        {
	  ui_mode = color_profiling;
          preview_display_scheduled = true;
          display_scheduled = true;
	  print_help ();
	  printf ("Color profiling mode\n");
        }
      if (k == 'X' && scan.has_rgb ())
        {
	  rparams.compute_mix_weights (patch_proportions (current.type, &rparams));
          setvals ();
        }
    }
  else
    {
      if (k >= '1' && k <= (int)render_type_max-(int)render_type_first_scr_detect + 1 + '1')
	{
	  scr_detect_display_type = k - '1';
	  display_scheduled = true;
	}
      if (k == 'd' && ! (event->state & GDK_CONTROL_MASK))
	setcolor = 1;
      if (k == 'r')
	setcolor = 2;
      if (k == 'g')
	setcolor = 3;
      if (k == 'b')
	setcolor = 4;
      if (k == 'P' || k == 'O')
	{
	  gdouble scale_x, scale_y;
	  gint shift_x, shift_y;
	  gint pxsize =  /*gtk_image_viewer_get_image_width (GTK_IMAGE_VIEWER (data.image_viewer))*/1024;
	  gint pysize =  /*gtk_image_viewer_get_image_height (GTK_IMAGE_VIEWER (data.image_viewer))*/800;
	  gtk_image_viewer_get_scale_and_shift (GTK_IMAGE_VIEWER (data.image_viewer),
						&scale_x, &scale_y, &shift_x,
						&shift_y);
	  int minx = std::max ((int)(shift_x / scale_x), 0);
	  int maxx = std::min ((int)((shift_x + pxsize) / scale_x), scan.width);
	  int miny = std::max ((int)(shift_y / scale_y), 0);
	  int maxy = std::min ((int)((shift_y + pysize) / scale_y), scan.height);
	  file_progress_info progress (stdout);
	  analyze_color_proportions (current_scr_detect, rparams, scan, k == 'O' ? &current : NULL, minx, miny, maxx, maxy, &progress);
	}
    }

  return FALSE;
}

void destroy(GtkWidget *widget, gpointer *data)
{
    gtk_main_quit();
}

/* Initialize the GUI.  */
static GtkWidget *
initgtk (int *argc, char **argv)
{
  GtkBuilder *builder;
  GtkWidget *window;
  GtkWidget *image_viewer;

  gtk_init (argc, &argv);
#if 0
  std::filesystem::path path(binary_name);
  std::string filename = path.parent_path() + "/../colorscreen/gtkgui.glade";
#endif


  /* Create builder and load interface */
  builder = gtk_builder_new ();
  if (!gtk_builder_add_from_file (builder,
      DATADIR "/colorscreen/gtkgui.glade", NULL)
#if 0
      && !gtk_builder_add_from_file (builder, filename.c_str (), NULL))
#endif
      && !gtk_builder_add_from_file (builder, "gtkgui.glade", NULL)
      && !gtk_builder_add_from_file (builder, "../share/colorscreen/gtkgui.glade", NULL))
    {
      fprintf (stderr, "Can not open " DATADIR "/colorscreen/gtkgui.glade\n");
      exit (1);
    }

  /* Obtain widgets that we need */
  window = GTK_WIDGET (gtk_builder_get_object (builder, "window"));
  gtk_signal_connect (GTK_OBJECT (window), "destroy", (GtkSignalFunc) destroy, NULL);
  data.save = GTK_WIDGET (gtk_builder_get_object (builder, "save"));
  /*data.bigimage = GTK_IMAGE (gtk_builder_get_object (builder, "bigimage")); */
  data.smallimage =
    GTK_IMAGE (gtk_builder_get_object (builder, "smallimage"));
  data.maindisplay_scroll =
    GTK_WIDGET (gtk_builder_get_object (builder, "maindisplay-scroll"));

  /* Add image_viewer.  */
  image_viewer = gtk_image_viewer_new (NULL);
  g_signal_connect (image_viewer,
		    "image-annotate", G_CALLBACK (cb_image_annotate), NULL);

  gtk_signal_connect (GTK_OBJECT (image_viewer), "key_press_event",
		      GTK_SIGNAL_FUNC (cb_key_press_event), NULL);
  /*gtk_signal_connect (GTK_OBJECT(image_viewer),     "button_press_event",
     GTK_SIGNAL_FUNC(cb_press), NULL); */
  data.image_viewer = image_viewer;

  gtk_container_add (GTK_CONTAINER (data.maindisplay_scroll), image_viewer);

  gtk_widget_show (image_viewer);

  // Set the scroll region and zoom range
  gtk_image_viewer_set_scroll_region (GTK_IMAGE_VIEWER (image_viewer),
				      20, 20, scan.width - 20, scan.height - 20);
  gtk_image_viewer_set_zoom_range (GTK_IMAGE_VIEWER (image_viewer), 0.01, 64);
  gtk_image_viewer_set_scale_and_shift (GTK_IMAGE_VIEWER (image_viewer), 4.0,
					4.0, 64, 64);

  // Need to do a manual zoom fit at creation because a bug when
  // not using an image.
  gtk_image_viewer_zoom_fit (GTK_IMAGE_VIEWER (image_viewer));
  data.gamma = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "gamma"));
  data.scanner_mtf_sigma = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "mtf-sigma"));
  data.saturation = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "saturation"));
  data.presaturation = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "presaturation"));
  data.sharpen_radius = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "sharpen_radius"));
  data.sharpen_amount = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "sharpen_amount"));
  data.y2 = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "y2"));
  data.brightness = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "brightness"));
  data.scanner_snr = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "scanner_snr"));
  data.RL_iterations = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "RL_iterations"));
  data.sigma = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "sigma"));
  data.tilt_x = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "tilt_x"));
  data.tilt_y = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "tilt_y"));
  data.scanner_mtf_scale = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "scanner_mtf_scale"));
  data.dark_point = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "dark_point"));
  data.collection_threshold = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "collection_threshold"));
  data.balance_black = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "balance_black"));
  data.mix_red = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "mix_red"));
  data.mix_green = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "mix_green"));
  data.mix_blue = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "mix_blue"));
  data.balance_red = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "balance_red"));
  data.balance_green = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "balance_green"));
  data.balance_blue = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "balance_blue"));
  /*data.about = GTK_WIDGET( gtk_builder_get_object( builder, "aboutdialog1" ) ); */

  /* Connect callbacks */
  gtk_builder_connect_signals (builder, &data);

  /* Destroy builder */
  g_object_unref (G_OBJECT (builder));
  return window;
}

static struct scr_to_img_parameters &
get_scr_to_img_parameters ()
{
  current.mesh_trans = current_mesh;
  return current;
}

static inline void
init_transformation_data (scr_to_img *trans)
{
  trans->set_parameters (get_scr_to_img_parameters (), scan);
}

/* This renders the small preview widget.   */

static void
previewrender (GdkPixbuf ** pixbuf)
{
  guint8 *pixels;
  if (scan.stitch || current.type == Random)
    return;
  enum render_parameters::color_model_t cm = rparams.color_model;
  //if (optimize_colors)
    //rparams.color_model = render_parameters::color_model_optimized;
  int my_xsize = PREVIEWSIZE, my_ysize = PREVIEWSIZE;

  g_object_unref (*pixbuf);
  *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, 0, 8, my_xsize, my_ysize);

  pixels = gdk_pixbuf_get_pixels (*pixbuf);
  int rowstride = gdk_pixbuf_get_rowstride (*pixbuf);
  file_progress_info progress (stdout);
  render_preview (scan, get_scr_to_img_parameters (), rparams, pixels, my_xsize, my_ysize, rowstride, &progress);
  cairo_surface_t *surface
    = cairo_image_surface_create_for_data (pixels,
					   CAIRO_FORMAT_RGB24,
					   my_xsize,
					   my_ysize,
					   rowstride);
#if 0
  if (current_solver.npoints)
    {
      scr_to_img map;
      map.set_parameters (current, scan);
      for (int i = 0; i <current_solver.npoints; i++)
	{
	  draw_circle (surface, bigscale, xoffset, yoffset, current_solver.point[i].img_x, current_solver.point[i].img_y, 1, 0, 1);
	  coord_t sx, sy;
	  map.to_img (current_solver.point[i].screen_x, current_solver.point[i].screen_y, &sx, &sy);
	  draw_circle (surface, bigscale, xoffset, yoffset, sx, sy, 0, 1, 1);
	}
    }
#endif
  cairo_surface_destroy (surface);
  rparams.color_model = cm;
}

static void
draw_circle (cairo_surface_t *surface, coord_t bigscale,
    	     int xoffset, int yoffset, int width, int height,
    	     coord_t x, coord_t y, coord_t r, coord_t g, coord_t b, coord_t radius = 3, luminosity_t opacity = 0.5)
{
  if ((x + radius) * bigscale - xoffset < 0
      || (y + radius) * bigscale - yoffset < 0
      || (x - radius) * bigscale - xoffset > width 
      || (y - radius) * bigscale - yoffset > height)
    return;

  cairo_t *cr = cairo_create (surface);
  cairo_translate (cr, -xoffset, -yoffset);
  cairo_scale (cr, bigscale, bigscale);

  cairo_set_source_rgba (cr, r, g, b, opacity);
  cairo_arc (cr, x, y, radius, 0.0, 2 * G_PI);

  cairo_fill (cr);
  cairo_destroy (cr);
}

static void
draw_line (cairo_surface_t *surface, coord_t bigscale,
	   int xoffset, int yoffset,
	   coord_t x1, coord_t y1, coord_t x2, coord_t y2, coord_t r, coord_t g, coord_t b, coord_t width = 1, luminosity_t opacity = 0.5)
{
  cairo_t *cr = cairo_create (surface);
  cairo_translate (cr, -xoffset, -yoffset);
  cairo_scale (cr, bigscale, bigscale);

  cairo_set_line_width (cr, std::max (width/bigscale, (coord_t)1));
  cairo_set_source_rgba (cr, r, g, b, opacity);
  cairo_move_to (cr, x1, y1);
  cairo_line_to (cr, x2, y2);

  cairo_stroke (cr);
  cairo_destroy (cr);
}
static void
draw_rectangle (cairo_surface_t *surface, coord_t bigscale,
	   int xoffset, int yoffset,
	   coord_t x1, coord_t y1, coord_t x2, coord_t y2, coord_t r, coord_t g, coord_t b, coord_t width = 1, luminosity_t opacity = 0.5)
{
  cairo_t *cr = cairo_create (surface);
  cairo_translate (cr, -xoffset, -yoffset);
  cairo_scale (cr, bigscale, bigscale);

  cairo_set_line_width (cr, std::max (width/bigscale, (coord_t)1));
  cairo_set_source_rgba (cr, r, g, b, opacity);
  cairo_move_to (cr, x1, y1);
  cairo_line_to (cr, x2, y1);
  cairo_line_to (cr, x2, y2);
  cairo_line_to (cr, x1, y2);
  cairo_line_to (cr, x1, y1);

  cairo_stroke (cr);
  cairo_destroy (cr);
}

void
draw_text (cairo_surface_t *surface, coord_t bigscale,
	   int xoffset, int yoffset, char *text,
	   coord_t x, coord_t y, coord_t r, coord_t g, coord_t b, coord_t width = 1, luminosity_t opacity = 0.5)
{
  cairo_t *cr = cairo_create (surface);
  cairo_translate (cr, -xoffset, -yoffset);
  cairo_scale (cr, bigscale, bigscale);

  cairo_set_source_rgb(cr, r, g, b);

  cairo_select_font_face(cr, "Helvetica",
      CAIRO_FONT_SLANT_NORMAL,
      CAIRO_FONT_WEIGHT_BOLD);

  cairo_set_font_size(cr, 25 / bigscale);

  cairo_move_to(cr, x, y);
  cairo_show_text(cr, text);


  cairo_stroke (cr);
  cairo_destroy (cr);
}

static void
bigrender (int xoffset, int yoffset, coord_t bigscale, GdkPixbuf * bigpixbuf)
{
  int bigrowstride = gdk_pixbuf_get_rowstride (bigpixbuf);
  guint8 *bigpixels = gdk_pixbuf_get_pixels (bigpixbuf);
  int pxsize = gdk_pixbuf_get_width (bigpixbuf);
  int pysize = gdk_pixbuf_get_height (bigpixbuf);
  coord_t step = 1 / bigscale;
  bool ret;

  {
    file_progress_info progress (stdout);
    render_type_parameters rtparam;
    if (ui_mode == screen_detection && scr_detect_display_type)
      {
	rtparam.type = (enum render_type_t)(scr_detect_display_type + (int)render_type_first_scr_detect - 1);
	rtparam.color = color_display;
      }
    else
      {
	rtparam.type = ui_mode == screen_detection ? render_type_original : (enum render_type_t)display_type;
	rtparam.color = color_display;
      }
    tile_parameters tile;
    tile.width = pxsize;
    tile.height = pysize;
    tile.rowstride = bigrowstride;
    tile.pixelbytes = 4;
    tile.pos = {(coord_t)xoffset, (coord_t)yoffset};
    tile.step = step;
    tile.pixels = bigpixels;
    ret = render_tile (scan, get_scr_to_img_parameters (), current_scr_detect, rparams, rtparam, tile,&progress);
  }

  cairo_surface_t *surface
    = cairo_image_surface_create_for_data (bigpixels,
					   CAIRO_FORMAT_RGB24,
					   pxsize,
					   pysize,
					   bigrowstride);
  draw_circle (surface, bigscale, xoffset, yoffset, pxsize, pysize, current.center.x, current.center.y, 0, 0, 1);
  draw_circle (surface, bigscale, xoffset, yoffset, pxsize, pysize, current.center.x + current.coordinate1.x, current.center.y + current.coordinate1.y, 1, 0, 0);
  draw_circle (surface, bigscale, xoffset, yoffset, pxsize, pysize, current.center.x + current.coordinate2.x, current.center.y + current.coordinate2.y, 0, 1, 0);

  draw_circle (surface, bigscale, xoffset, yoffset, pxsize, pysize, current.lens_correction.center.x * scan.width, current.lens_correction.center.y * scan.height, 1, 0, 1);

  if (detected.smap && bigscale >= 1)
    for (int y = 0; y < detected.smap->height; y++)
      for (int x = 0; x < detected.smap->width; x++)
        {
	  int xx = x - detected.smap->xshift;
	  int yy = y - detected.smap->yshift;
	    if (detected.smap->known_p ({xx, yy}))
              {
		solver_parameters::point_color t;
		point_t scr = detected.smap->get_screen_coord ({xx, yy}, &t);
		point_t img = detected.smap->get_coord ({xx, yy});
		draw_circle (surface, bigscale, xoffset, yoffset, pxsize, pysize, img.x, img.y, t == solver_parameters::blue, t == solver_parameters::green, t == solver_parameters::red, 1);
              }
         }

  if (ui_mode == color_profiling && color_optimizer_points.size ())
    {
      scr_to_img map;
      map.set_parameters (current, scan);
      bradford_whitepoint_adaptation_matrix m(d50_white, srgb_white);
      for (size_t i = 0; i < color_optimizer_points.size (); i++)
        {
	  point_t p;
	  if (!scan.stitch)
	    p = map.to_img ({color_optimizer_points[i].x, color_optimizer_points[i].y});
	  else
	    {
	      p = scan.stitch->common_scr_to_img.scr_to_final ({color_optimizer_points[i].x, color_optimizer_points[i].y});
	      p.x -= scan.xmin;
	      p.y -= scan.ymin;
	    }
	  rgbdata c2 = {1, 0, 0};
	  rgbdata c1 = {1, 1, 1};
	  if (color_optimizer_match.size () > i)
	    {
	      xyz c = color_optimizer_match[i].target;
	      m.apply_to_rgb (c.x, c.y, c.z, &c.x, &c.y, &c.z);
	      c.to_srgb (&c1.red, &c1.green, &c1.blue);
	      c1 = c1.clamp ();
	      c = color_optimizer_match[i].profiled;
	      m.apply_to_rgb (c.x, c.y, c.z, &c.x, &c.y, &c.z);
	      c.to_srgb (&c2.red, &c2.green, &c2.blue);
	      c2 = c2.clamp ();
	      char buf[256];
	      sprintf (buf, "%.1fΔE2k", color_optimizer_match[i].deltaE);
	      draw_text (surface, bigscale, xoffset, yoffset, buf, p.x + 27 / bigscale, p.y + 2 / bigscale, 0, 0, 0, 1);
	      draw_text (surface, bigscale, xoffset, yoffset, buf, p.x + 25 / bigscale, p.y, 1, 1, 1, 1);
	    }
	  draw_circle (surface, bigscale, xoffset, yoffset, pxsize, pysize, p.x, p.y, 1,1,1, 23/bigscale, 1);
	  draw_circle (surface, bigscale, xoffset, yoffset, pxsize, pysize, p.x, p.y, c2.blue, c2.green, c2.red, 20/bigscale, 1);
          draw_circle (surface, bigscale, xoffset, yoffset, pxsize, pysize, p.x, p.y, c1.blue, c1.green, c1.red, 10/bigscale, 1);
        }
    }

  if (current.n_motor_corrections && (ui_mode == motor_correction_editing || ui_mode == solver_editing))
    for (int i = 0; i < current.n_motor_corrections; i++)
      {
	if (current.scanner_type == lens_move_horisontally || current.scanner_type == fixed_lens_sensor_move_horisontally)
	  {
	    draw_line (surface, bigscale, xoffset, yoffset, current.motor_correction_x[i], 0, current.motor_correction_x[i], scan.height, 1, 1, 0);
	    draw_line (surface, bigscale, xoffset, yoffset, current.motor_correction_y[i], 0, current.motor_correction_y[i], scan.height, 0, 1, 1);
	  }
	else if (current.scanner_type == lens_move_vertically || current.scanner_type == fixed_lens_sensor_move_vertically)
	  {
	    draw_line (surface, bigscale, xoffset, yoffset, 0, current.motor_correction_x[i], scan.width, current.motor_correction_x[i], 1, 1, 0);
	    draw_line (surface, bigscale, xoffset, yoffset, 0, current.motor_correction_y[i], scan.width, current.motor_correction_y[i], 0, 1, 1);
	  }
      }
  if (current_solver.n_points() && (ui_mode == motor_correction_editing || ui_mode == solver_editing))
    {
      scr_to_img map;
      map.set_parameters (current, scan);
      for (int i = 0; i <current_solver.n_points (); i++)
	{
	  coord_t xi = current_solver.points[i].img.x;
	  coord_t yi = current_solver.points[i].img.y;
	  rgbdata color = current_solver.points[i].get_rgb ();
	  point_t scr_p = current_solver.points[i].scr;

	  /* If we do only 1D solving, provide correct y coordinate to minimize distance.  */
	  if (screen_with_vertical_strips_p (current.type))
	    {
	      point_t scr2 = map.to_scr (current_solver.points[i].img);
	      scr_p.y = scr2.y;
	    }
	  point_t p = map.to_img (scr_p);

	  draw_circle (surface, bigscale, xoffset, yoffset, pxsize, pysize, xi, yi, color.blue, color.green, color.red);
	  draw_circle (surface, bigscale, xoffset, yoffset, pxsize, pysize, p.x, p.y, 3*color.blue/4, 3*color.green/4, 3*color.red/4);

	  coord_t patch_diam = sqrt (current.coordinate1.x * current.coordinate1.x + current.coordinate1.y * current.coordinate1.y) / 2;
	  double scale = 200 / patch_diam / bigscale;
	  coord_t xd = p.x - xi;
	  coord_t yd = p.y - yi;
	  bool bad = sqrt (xd*xd + yd*yd) > patch_diam / 4;

	  xd *= scale;
	  yd *= scale;

	  if (bad)
	    draw_line (surface, bigscale, xoffset, yoffset, xi, yi, xi + xd, yi + yd, 0, 0, 1, 16);
	  draw_line (surface, bigscale, xoffset, yoffset, xi, yi, xi + xd, yi + yd, color.blue, color.green, color.red, 6);
	  draw_line (surface, bigscale, xoffset, yoffset, xi + xd, yi + yd, xi + xd * 0.7 + yd * 0.2, yi + yd * 0.7 - xd * 0.2, color.blue, color.green, color.red, 6);
	  draw_line (surface, bigscale, xoffset, yoffset, xi + xd, yi + yd, xi + xd * 0.7 - yd * 0.2, yi + yd * 0.7 + xd * 0.2, color.blue, color.green, color.red, 6);
	  draw_line (surface, bigscale, xoffset, yoffset, xi, yi, xi + xd, yi + yd, 1, 1, 1, 3);
	}
    }
  if (ui_mode == solver_editing)
    {
      if (sel1x != sel2x || sel1y != sel2y)
	draw_rectangle (surface, bigscale, xoffset, yoffset, sel1x, sel1y, sel2x, sel2y, 1,1,1, 6);
    }


  cairo_surface_destroy (surface);
}
static void
display_preview ()
{
  previewrender (&data.smallpixbuf);
  gtk_image_set_from_pixbuf (data.smallimage, data.smallpixbuf);
}

static void
display ()
{
  gtk_image_viewer_redraw ((GtkImageViewer *)data.image_viewer, 1);
}

G_MODULE_EXPORT void
cb_redraw (GtkButton * button, Data * data)
{
  if (!initialized)
    return;
  getvals ();
  display ();
}

G_MODULE_EXPORT void
cb_press_small (GtkImage * image, GdkEventButton * event, Data * data)
{
  getvals ();
  if (event->button == 1)
    {
      offsetx =
	8 +
	(event->x) * scan.width * bigscale /
	gdk_pixbuf_get_width (data->smallpixbuf);
      offsety =
	8 +
	(event->y) * scan.height * bigscale /
	gdk_pixbuf_get_height (data->smallpixbuf);
    }
}

static double xpress, ypress;
static double xpress1, ypress1;


static bool button1_pressed;
static bool button3_pressed;
static struct scr_to_img_parameters press_parameters;
static int current_motor_correction = -1;
static double current_motor_correction_val;

static gdouble saved_scale_x, saved_scale_y;
static gint saved_shift_x, saved_shift_y;
static int saved_display_type;
bool zoom_saved = false;

G_MODULE_EXPORT void
cb_press (GtkImage * image, GdkEventButton * event, Data * data2)
{
  gdouble scale_x, scale_y;
  gint shift_x, shift_y;
  gtk_image_viewer_get_scale_and_shift (GTK_IMAGE_VIEWER (data.image_viewer),
					&scale_x, &scale_y, &shift_x,
					&shift_y);
  if (!initialized)
    return;
#if 0
  if (event->button == 1 && 0)
    {
      double newcenter.x = (event->x + shift_x) / scale_x;
      double newcenter.y = (event->y + shift_y) / scale_y;
      solver_parameters::point_t p;
      file_progress_info progress (stdout);
      finetune (rparams, current, scan, p, newcenter.x, newcenter.y, &progress);
      setvals ();
      display_scheduled = true;
    }
#endif
  //printf ("Press %i\n",ui_mode == color_profiling);
  if (ui_mode == color_profiling)
    {
      coord_t x = (event->x + shift_x) / scale_x;
      coord_t y = (event->y + shift_y) / scale_y;
      coord_t screenx, screeny;
      scr_to_img map;
      map.set_parameters (current, scan);
      point_t screen;
     
      if (!scan.stitch)
        screen = map.to_scr ({x, y});
      else
        screen = scan.stitch->common_scr_to_img.final_to_scr ({x + scan.xmin, y + scan.ymin});
      if (event->button == 1)
	{
	  color_optimizer_points.push_back (screen);
          display_scheduled = true;
	  return;
	}
      else if (event->button >= 2 && color_optimizer_points.size ());
        {
	  int best = 0;
	  coord_t delta = 10000000;
	  for (int i = 0; i < color_optimizer_points.size (); i++)
	    {
	      coord_t dist = sqrt ((color_optimizer_points[i].x-screen.x) * (color_optimizer_points[i].x-screen.x) + (color_optimizer_points[i].y-screen.y) * (color_optimizer_points[i].y-screen.y));
	      if (dist < delta)
	        {
		   best = i;
		   delta = dist;
	        }
	    }
	  for (int i = best; i < color_optimizer_points.size () - 1; i++)
	    color_optimizer_points[i] = color_optimizer_points[i + 1];
	  color_optimizer_points.pop_back ();
	  display_scheduled = true;
        }
      
    }
  if (ui_mode == screen_detection)
    {
      if (setcolor && event->button == 1)
	{
	    int px = (event->x + shift_x) / scale_x + 0.5;
	    int py = (event->y + shift_y) / scale_y + 0.5;
	    if (px < 0 || px >= scan.width || py < 0 || py >= scan.height)
	      {
		setcolor = 0;
		return;
	      }
	    luminosity_t r = scan.rgbdata[py][px].r / (luminosity_t)scan.maxval;;
	    luminosity_t g = scan.rgbdata[py][px].g / (luminosity_t)scan.maxval;;
	    luminosity_t b = scan.rgbdata[py][px].b / (luminosity_t)scan.maxval;;
	    if (setcolor == 1)
	      {
	        current_scr_detect.black.red = r;
	        current_scr_detect.black.green = g;
	        current_scr_detect.black.blue = b;
		printf ("Black %f %f %f\n",r,g,b);
	      }
	    if (setcolor == 2)
	      {
	        current_scr_detect.red.red = r;
	        current_scr_detect.red.green = g;
	        current_scr_detect.red.blue = b;
	      }
	    if (setcolor == 3)
	      {
	        current_scr_detect.green.red = r;
	        current_scr_detect.green.green = g;
	        current_scr_detect.green.blue = b;
	      }
	    if (setcolor == 4)
	      {
	        current_scr_detect.blue.red = r;
	        current_scr_detect.blue.green = g;
	        current_scr_detect.blue.blue = b;
	      }
	    setvals ();
	    display_scheduled = true;
	    setcolor = 0;
	    //preview_display_scheduled = true;
	}
    }
  else if (ui_mode == motor_correction_editing)
    {
      double x = (event->x + shift_x) / scale_x;
      double y = (event->y + shift_y) / scale_y;
      double click;
      double scale;
      if (current.scanner_type == lens_move_horisontally || current.scanner_type == fixed_lens_sensor_move_horisontally)
	{
	  click = x;
	  scale = scale_x;
	}
      else
	{
	  click = y;
	  scale = scale_y;
	}

      if (current.scanner_type != fixed_lens && event->button == 1)
	{
	  int i;
	  int best_i = -1;
	  double min_dist = 5 / scale_x;

	  save_parameters ();

	  for (i = 0; i < current.n_motor_corrections; i++)
	    {
	      double dist = fabs (click - current.motor_correction_x[i]);
	      if (dist < min_dist)
		{
		  best_i = i;
		  min_dist = dist;
		}
	    }
	  if (best_i >= 0)
	    {
	      current_motor_correction = best_i;
	      current_motor_correction_val = current.motor_correction_x[best_i];
	      printf ("Found %i\n", best_i);
	    }
	  else
	    {
	      current_motor_correction = current.add_motor_correction_point (click, click);
	      current_motor_correction_val = click;
	    }
	  display_scheduled = true;
	  xpress1 = event->x;
	  ypress1 = event->y;
	  button1_pressed = true;
	}
      if (current.scanner_type != fixed_lens && event->button == 3)
	{
	  double min_dist = 5 / scale;
	  int best_i = -1;
	  for (int i = 0; i < current.n_motor_corrections; i++)
	    {
	      double dist = fabs (click - current.motor_correction_x[i]);
	      if (dist < min_dist)
		{
		  best_i = i;
		  min_dist = dist;
		}
	    }
	  if (best_i >= 0)
	    {
	       save_parameters ();
	       display_scheduled = true;
	       preview_display_scheduled = true;
	       current.remove_motor_correction_point (best_i);
	    }
	}
    }
  else if (ui_mode == solver_editing)
    {
      coord_t x = (event->x + shift_x) / scale_x;
      coord_t y = (event->y + shift_y) / scale_y;
      xpress = x;
      ypress = y;
      coord_t screenx, screeny;
      coord_t pscreenx, pscreeny;
      coord_t rscreenx, rscreeny;
      scr_to_img map;
      map.set_parameters (current, scan);
      point_t screen = map.to_scr ({x, y});
      pscreenx = floor (screen.x);
      pscreeny = floor (screen.y);
      rscreenx = pscreenx;
      rscreeny = pscreenx;
      struct coord {coord_t x, y;
      		    solver_parameters::point_color color;};
      enum solver_parameters::point_color rcolor = solver_parameters::green;
      int npoints;
      if (current.type != Random)
	{
	  struct solver_parameters::point_location *points = solver_parameters::get_point_locations (current.type, &npoints);
	  for (int i = 0; i < npoints; i++)
	    {
	      coord_t qscreenx = pscreenx + points[i].x;
	      coord_t qscreeny = pscreeny + points[i].y;
	      if ((screenx - rscreenx) * (screenx - rscreenx) + (screeny - rscreeny) * (screeny - rscreeny)
		  >(screenx - qscreenx) * (screenx - qscreenx) + (screeny - qscreeny) * (screeny - qscreeny))
		{
		    rscreenx = qscreenx;
		    rscreeny = qscreeny;
		    rcolor = points[i].color;
		}
	    }
	}
      if (event->button == 3)
	{
	  int n;
	  int best_n = -1;
	  double best_dist = INT_MAX;
	  for (n = 0; n < current_solver.n_points (); n++)
	    {
	      double dist = sqrt ((current_solver.points[n].img.x - x) * (current_solver.points[n].img.x - x) + (current_solver.points[n].img.y - y) * (current_solver.points[n].img.y - y));
	      if (dist < 5 * std::min (scale_x, (gdouble)1) && dist < best_dist)
		{
		  best_n = n;
		  best_dist = dist;
		}
	    }

	  if (best_n > 0)
	    {
	      current_solver.remove_point (best_n);
	      maybe_solve ();
	      display_scheduled = true;
	    }
	}
    }
  else
    {
      if (event->button == 1 && setcenter)
	{
	  point_t newcenter = {(event->x + shift_x) / scale_x, (event->y + shift_y) / scale_y};
          setcenter = false;
	  if (newcenter != current.center)
	    {
	      current.center = newcenter;
	      setvals ();
	      display_scheduled = true;
	      preview_display_scheduled = true;
	    }
	}
      if (event->button == 1 && set_lens_center)
	{
	  point_t newcenter = {(event->x + shift_x) / scale_x, (event->y + shift_y) / scale_y};
          set_lens_center = false;
	  if (newcenter != current.lens_correction.center)
	    {
	      current.lens_correction.center = newcenter;
	      setvals ();
	      display_scheduled = true;
	      preview_display_scheduled = true;
	      maybe_solve ();
	    }
	}
      press_parameters = current;
      if (event->button == 1)
	{
	  xpress1 = event->x;
	  ypress1 = event->y;
	  button1_pressed = true;
	}
      else if (event->button == 3)
	{
	  xpress = (event->x + shift_x) / scale_x;
	  ypress = (event->y + shift_y) / scale_y;
	  button3_pressed = true;
	}
    }
}

void
handle_drag (int x, int y, int button)
{
  if (ui_mode == screen_detection)
    return;
  gdouble scale_x, scale_y;
  gint shift_x, shift_y;
  gtk_image_viewer_get_scale_and_shift (GTK_IMAGE_VIEWER
					(data.image_viewer), &scale_x,
					&scale_y, &shift_x, &shift_y);
  if (ui_mode == motor_correction_editing)
    {
      if (current_motor_correction >= 0 && button == 1)
	{
	  double xoffset = (x - xpress1) / scale_x;
	  double yoffset = (y - ypress1) / scale_y;
	  if (current.scanner_type == lens_move_horisontally || current.scanner_type == fixed_lens_sensor_move_horisontally)
	    current.motor_correction_x[current_motor_correction] = current_motor_correction_val + xoffset;
	  else
	    current.motor_correction_x[current_motor_correction] = current_motor_correction_val + yoffset;
	  setvals ();
	  display_scheduled = true;
	  preview_display_scheduled = true;
	  for (int i = 0; i < current.n_motor_corrections; i++)
	    {
	      printf (" %f:%f", current.motor_correction_x[i], current.motor_correction_y[i]);
	    }
	  printf ("\n");
	}
      return;
    }
  else if (ui_mode == solver_editing)
    {
      coord_t xx = (x + shift_x) / scale_x;
      coord_t yy = (y + shift_y) / scale_y;
      if (xx != xpress && yy != ypress && button == 1)
        {
	  if (xpress < xx)
	    {
	      sel1x = xpress;
	      sel2x = xx;
	    }
	  else
	    {
	      sel2x = xpress;
	      sel1x = xx;
	    }
	  if (ypress < yy)
	    {
	      sel1y = ypress;
	      sel2y = yy;
	    }
	  else
	    {
	      sel2y = ypress;
	      sel1y = yy;
	    }
	  display_scheduled = true;
	  //printf ("selection %f %f %f %f\n",sel1x, sel1y,sel2x,sel2y);
        }
      return;
    }
  else if (ui_mode == color_profiling)
    return;
  if (button == 1)
    {
      double xoffset = (x - xpress1) / scale_x;
      double yoffset = (y - ypress1) / scale_y;
      if (current.center.x == press_parameters.center.x + xoffset
          && current.center.y == press_parameters.center.y + yoffset)
	return;
      current.center.x = press_parameters.center.x + xoffset;
      current.center.y = press_parameters.center.y + yoffset;
      setvals ();
      display_scheduled = true;
      preview_display_scheduled = true;
    }
  else if (button == 3)
    {
      double x1 = (xpress - current.center.x);
      double y1 = (ypress - current.center.y);
      double x2 = (x + shift_x) / scale_x - current.center.x;
      double y2 = (y + shift_y) / scale_y - current.center.y;
      double scale = sqrt ((x2 * x2) + (y2 * y2))/sqrt ((x1*x1) + (y1*y1));
      double angle = atan2f (y2, x2) - atan2f (y1, x1);
      if (!angle)
	return;
      if (!freeze_x)
	{
	  current.coordinate1.x = (press_parameters.coordinate1.x * cos (angle)
				  - press_parameters.coordinate1.y * sin (angle)) * scale;
	  current.coordinate1.y = (press_parameters.coordinate1.x * sin (angle)
				  + press_parameters.coordinate1.y * cos (angle)) * scale;
	}
      if (!freeze_y)
	{
	  current.coordinate2.x = (press_parameters.coordinate2.x * cos (angle)
				  - press_parameters.coordinate2.y * sin (angle)) * scale;
	  current.coordinate2.y = (press_parameters.coordinate2.x * sin (angle)
				  + press_parameters.coordinate2.y * cos (angle)) * scale;
	}
      setvals ();
      display_scheduled = true;
      preview_display_scheduled = true;
    }
}

G_MODULE_EXPORT void
cb_release (GtkImage * image, GdkEventButton * event, Data * data2)
{
  handle_drag (event->x, event->y, event->button);
  save_parameters ();
  gdouble scale_x, scale_y;
  gint shift_x, shift_y;
  gtk_image_viewer_get_scale_and_shift (GTK_IMAGE_VIEWER (data.image_viewer),
					&scale_x, &scale_y, &shift_x,
					&shift_y);
  if (zoom_saved)
    {
      gtk_image_viewer_set_scale_and_shift (GTK_IMAGE_VIEWER (data.image_viewer),
					    saved_scale_x, saved_scale_y, saved_shift_x, saved_shift_y);
      zoom_saved = false;
      display_type = saved_display_type;
    }
  if (ui_mode == solver_editing && event->button == 1)
    {
      coord_t x = (event->x + shift_x) / scale_x;
      coord_t y = (event->y + shift_y) / scale_y;
      if (finetuning && x == xpress && y == ypress)
        {
	  printf ("Finetuning %f %f\n",x,y);
	  finetune_parameters fparam;
	  fparam.multitile = scale_x > 1 ? 3 : 1;
	  fparam.flags |= finetune_position | finetune_bw | finetune_verbose | finetune_use_srip_widths;
	  if (tmphack)
	    {
	      fparam.simulated_file = "/tmp/bwsimulated.tif";
	      fparam.orig_file = "/tmp/bworig.tif";
	      fparam.diff_file = "/tmp/bwdiff.tif";
	      fparam.screen_file = "/tmp/bwscr.tif";
	      fparam.screen_blur_file = "/tmp/bwscr-blur.tif";
	      fparam.collected_file = "/tmp/bwscr-collected.tif";
	      fparam.dot_spread_file = "/tmp/bwdot-spread.tif";
	    }
	  file_progress_info progress (stdout);
	  finetune_result res = finetune (rparams, current, scan, {{(coord_t)x, (coord_t)y}}, NULL, fparam, &progress);
	  if (res.success)
	    {
	      current_solver.add_point (res.solver_point_img_location, res.solver_point_screen_location, res.solver_point_color);
	      display_scheduled = true;
	      if (tmphack && 0)
		system(bwcmd);
	      maybe_solve ();
	    }
          button1_pressed = false;
	  return;
        }
      if (x == xpress && y == ypress)
	{
	  const int desired_zoom = 16;
	  if (scale_x < desired_zoom && event->button == 1)
	    {
	      zoom_saved = true;
	      saved_display_type = display_type;
	      display_type = 0;
	      gtk_image_viewer_get_scale_and_shift (GTK_IMAGE_VIEWER (data.image_viewer),
						    &saved_scale_x, &saved_scale_y, &saved_shift_x,
						    &saved_shift_y);
	      gtk_image_viewer_set_scale_and_shift (GTK_IMAGE_VIEWER (data.image_viewer),
						    desired_zoom, desired_zoom, desired_zoom * x - event->x, desired_zoom * y - event->y);
	      return;
	    }
	  coord_t screenx, screeny;
	  coord_t pscreenx, pscreeny;
	  coord_t rscreenx, rscreeny;
	  scr_to_img map;
	  map.set_parameters (current, scan);
	  point_t screen = map.to_scr ({x, y});
	  pscreenx = floor (screen.x);
	  pscreeny = floor (screen.y);
	  rscreenx = pscreenx;
	  rscreeny = pscreenx;
	  struct coord {coord_t x, y;
			solver_parameters::point_color color;};
	  enum solver_parameters::point_color rcolor = solver_parameters::green;
	  int npoints;
	  struct solver_parameters::point_location *points = solver_parameters::get_point_locations (current.type, &npoints);
	  for (int i = 0; i < npoints; i++)
	    {
	      coord_t qscreenx = pscreenx + points[i].x;
	      coord_t qscreeny = pscreeny + points[i].y;
	      if ((screenx - rscreenx) * (screenx - rscreenx) + (screeny - rscreeny) * (screeny - rscreeny)
		  >(screenx - qscreenx) * (screenx - qscreenx) + (screeny - qscreeny) * (screeny - qscreeny))
		{
		    rscreenx = qscreenx;
		    rscreeny = qscreeny;
		    rcolor = points[i].color;
		}
	    }
	  if (event->button == 1)
	    {
	      if (set_solver_center)
		{
		  point_t newcenter = {(event->x + shift_x) / scale_x, (event->y + shift_y) / scale_y};
		  set_solver_center = false;
		  current_solver.weighted = true;
		  current_solver.center = newcenter;
		  printf ("Solver center: %f %f\n", newcenter.x, newcenter.y);
		  file_progress_info progress (stdout);
		  solver (&current, scan, current_solver, &progress);
		  preview_display_scheduled = true;
		  display_scheduled = true;
		  return;
		}
	      current_solver.add_point ({x, y}, {rscreenx, rscreeny}, rcolor);
#if 0
	      int n;
	      for (n = 0; n < n_solver_points; n++)
		if (solver_point[n].screen_x == rscreenx && solver_point[n].screen_y == rscreeny)
		  break;
	      if (n == n_solver_points)
		n_solver_points++;
	      solver_point[n].screen_x = rscreenx;
	      solver_point[n].screen_y = rscreeny;
	      solver_point[n].img_x = x;
	      solver_point[n].img_y = y;
	      for (int i =0; i < n_solver_points; i++)
		{
		  printf ("point %i img %f %f maps to scr %f %f\n", i, solver_point[i].img_x, solver_point[i].img_y, solver_point[i].screen_x, solver_point[i].screen_y);
		}
#endif
	      display_scheduled = true;
	      maybe_solve ();
	    }
	}
    }
  if (event->button == 1)
    button1_pressed = false;
  if (event->button == 3)
    button3_pressed = false;
}

G_MODULE_EXPORT void
cb_drag (GtkImage * image, GdkEventMotion * event, Data * data2)
{
  handle_drag (event->x, event->y,
	       button1_pressed ? 1 : button3_pressed ? 3 : 0);
}


G_MODULE_EXPORT void
cb_save (GtkButton * button, Data * data)
{
  FILE *out;
  out = fopen (paroname, "wt");
  if (!out)
    {
      perror (paroname);
    }
  if (!save_csp (out, &current, scan.has_rgb () ? &current_scr_detect : NULL, &rparams, &current_solver))
    {
      fprintf (stderr, "saving failed\n");
      exit (1);
    }
  fclose (out);
}


int
main (int argc, char **argv)
{
  GtkWidget *window;
  binary_name = argv[0];
  if (argc != 2 && argc != 3)
    {
      fprintf (stderr, "Invocation: %s <scan> <csp (optional)>]\n\n"
	       "scan must be either tiff of jpg file.\n"
	       "csp is where parameters should be load/stored.\n",
	       argv[0]);
      exit (1);
    }

  if (argc == 3)
    paroname = argv[2];
  else
    {
      paroname = strdup (argv[1]);
      int len = strlen (paroname), i;
      for (i = len - 1;i >= 0; i--)
	if (paroname[i]=='.')
	  break;
      if (i < 0 || len - i < 4)
	{
	  fprintf (stderr, "Can not find suffix\n");
	  exit (1);
	}
      paroname[i+1]='p';
      paroname[i+2]='a';
      paroname[i+3]='r';
      paroname[i+4]=0;
      printf ("Out file: %s\n", paroname);
    }

  FILE *in = fopen (paroname, "rt");
  const char *error;
  if (in && !load_csp (in, &current, &current_scr_detect, &rparams, &current_solver, &error))
    {
      fprintf (stderr, "Can not load parameters: %s\n", error);
      exit (1);
    }
  if (in)
    csp_loaded = true;
  //if (!in && scan.gamma != -2)
    //rparams.gamma = scan.gamma;
  current_mesh = current.mesh_trans;
  if (in)
    fclose (in);
  else
    {
      fprintf (stderr, "Can not open param file \"%s\": ", paroname);
      perror ("");
      if (in)
        exit (1);
    }
  save_parameters ();
    
  openimage (argv[1]);

  //current.mesh_trans = solver_mesh (&current, scan, current_solver);
#if 0
    new mesh (0, 0, 1, 1, 2, 2);
  current.mesh_trans->set_point (0,0, 0, 0);
  current.mesh_trans->set_point (0,1, 0, 100);
  current.mesh_trans->set_point (1,0, 100, 0);
  current.mesh_trans->set_point (1,1, 100, 100);
#endif



  current.mesh_trans = current_mesh;
  //save_csp (stdout, &current, scan.rgbdata ? &current_scr_detect : NULL, &rparams, &current_solver);
  window = initgtk (&argc, argv);
  setvals ();
  initialized = 1;
  data.smallpixbuf =
    gdk_pixbuf_new (GDK_COLORSPACE_RGB, 0, 8, (scan.width) / SCALE + 1,
		    (scan.height) / SCALE + 1);
  /* Show main window and start main loop */
  gtk_widget_show (window);
  int pxsize = 1024/*gdk_pixbuf_get_width (data.image_viewer);*/;
  int pysize = 800/*gdk_pixbuf_get_height (data.image_viewer);*/;
  double scale1 = pxsize / (double) scan.width;
  double scale2 = pysize / (double) scan.height;
  double scale =  std::max (std::min (scale1, scale2), 0.1);
  gtk_image_viewer_set_scale_and_shift (GTK_IMAGE_VIEWER ((GtkImageViewer *)data.image_viewer),
					scale, scale, 0, 0);
  gtk_image_viewer_redraw ((GtkImageViewer *)data.image_viewer, 1);

  while (true)
    {
      if (ui_mode == color_profiling
	  && (display_scheduled || preview_display_scheduled)
	  && color_optimizer_points.size ())
	{
	  file_progress_info progress (stdout);
	  optimize_color_model_colors (&current, scan, rparams, color_optimizer_points, &color_optimizer_match, &progress);
	}
      if (display_scheduled)
	{
	  display ();
	  display_scheduled = false;
	}
      if (preview_display_scheduled)
	{
	  display_preview ();
	  preview_display_scheduled = false;
	}
      gtk_main_iteration_do (true);
      while (gtk_events_pending ())
	{
	  gtk_main_iteration_do (FALSE);
	}
    }

  return (0);
}

}
