#include <time.h>
#include <stdio.h>
#include <gtk/gtk.h>
#include <assert.h>
#include <gtk/gtkbuilder.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <math.h>
#include <cairo.h>
#include <stdbool.h>

#include "config.h"
#include "gtk-image-viewer.h"
#include "../libcolorscreen/include/colorscreen.h"

#define UNDOLEVELS 100 
#define PREVIEWSIZE 600

extern "C" {

/* Undo history and the state of UI.  */
static struct scr_to_img_parameters undobuf[UNDOLEVELS];
static struct scr_to_img_parameters current;
static struct scr_detect_parameters undobuf_scr_detect[UNDOLEVELS];
static struct scr_detect_parameters current_scr_detect;
static int undopos;
/* Are we in screen detection mode?  */
bool scr_detect;

static char *oname, *paroname;
static void bigrender (int xoffset, int yoffset, coord_t bigscale, GdkPixbuf * bigpixbuf);

/* The graymap with original scan is stored here.  */
static image_data scan;
static int initialized = 0;
static render_parameters rparams;

/* Status of the main window.  */
static int offsetx = 8, offsety = 8;
static int bigscale = 4;
static bool color_display = false;

static int display_type = 0;
static int scr_detect_display_type = 0;
static bool display_scheduled = true;
static bool preview_display_scheduled = true;

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
  current = undobuf[undopos];
  current_scr_detect = undobuf_scr_detect[undopos];
  undopos = (undopos + UNDOLEVELS - 1) % UNDOLEVELS;
}
void
redo_parameters (void)
{
  undopos  = (undopos + 1) % UNDOLEVELS;
  current = undobuf[undopos];
  current_scr_detect = undobuf_scr_detect[undopos];
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
  GtkSpinButton *gamma, *screen_blur, *presaturation, *saturation, *y2, *brightness, *k1,
	       	*tilt_x_x, *tilt_x_y, *tilt_y_x, *tilt_y_y, *mix_gamma, *mix_red, *mix_green, *mix_blue;
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
openimage (int *argc, char **argv)
{
  const char *error;
  bool ret;
  {
    file_progress_info p (stdout, true);
    ret = scan.load (argv[1], &error, &p);
  }
  if (!ret)
    {
      fprintf (stderr, "%s\n", error);
      exit (1);
    }
  rparams.gray_min = 0;
  rparams.gray_max = scan.maxval;
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
  rparams.screen_blur_radius = gtk_spin_button_get_value (data.screen_blur);
  rparams.brightness = gtk_spin_button_get_value (data.brightness);
  current.tilt_x_x = gtk_spin_button_get_value (data.tilt_x_x);
  current.tilt_x_y = gtk_spin_button_get_value (data.tilt_x_y);
  current.tilt_y_x = gtk_spin_button_get_value (data.tilt_y_x);
  current.tilt_y_y = gtk_spin_button_get_value (data.tilt_y_y);
  rparams.mix_gamma = gtk_spin_button_get_value (data.mix_gamma);
  rparams.mix_red = gtk_spin_button_get_value (data.mix_red);
  rparams.mix_green = gtk_spin_button_get_value (data.mix_green);
  rparams.mix_blue = gtk_spin_button_get_value (data.mix_blue);
  current.k1 = gtk_spin_button_get_value (data.k1);
  if (rparams != old || current != old2)
    {
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
  gtk_spin_button_set_value (data.screen_blur, rparams.screen_blur_radius);
  gtk_spin_button_set_value (data.brightness, rparams.brightness);
  gtk_spin_button_set_value (data.tilt_x_x, current.tilt_x_x);
  gtk_spin_button_set_value (data.tilt_x_y, current.tilt_x_y);
  gtk_spin_button_set_value (data.tilt_y_x, current.tilt_y_x);
  gtk_spin_button_set_value (data.tilt_y_y, current.tilt_y_y);
  gtk_spin_button_set_value (data.mix_gamma, rparams.mix_gamma);
  gtk_spin_button_set_value (data.mix_red, rparams.mix_red);
  gtk_spin_button_set_value (data.mix_green, rparams.mix_green);
  gtk_spin_button_set_value (data.mix_blue, rparams.mix_blue);
  gtk_spin_button_set_value (data.k1, current.k1);
  initialized = 1;
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
static bool freeze_x = false;
static bool freeze_y = false;
static void display ();
static int setcolor;

static void
optimize (double xc, double yc, double cr, int stepsc, double x1, double y1,
	  double r1, int steps1, double r2, int steps2)
{
  scr_to_img_parameters best;
  scr_to_img_parameters c = current;
  double max = -1;
  static const int outertiles = 8;
  static const int innertiles = 128;
  save_parameters ();
  //double xxc = 0;
  //double yyc = 0;
  rparams.saturation = 4;
  rparams.screen_blur_radius = 0;
  printf ("optimization %i %i %i\n", stepsc, steps1, steps2);
//#pragma openmp parallel for
  for (double xx1 = x1 - r1; xx1 <= x1 + r1; xx1 += 2 * r1 / steps1)
    {
      for (double yy1 = y1 - r1; yy1 <= y1 + r1; yy1 += 2 * r1 / steps1)
	{
	  if (fabs (xx1) >= 3 * fabs (yy1))
	  for (double xxc = xc - cr; xxc <= xc + cr; xxc += 2 * cr / stepsc)
	    {
	      for (double yyc = yc - cr; yyc <= yc + cr;
		   yyc += 2 * cr / stepsc)
		{
		  for (double xx2 = 1 - r2; xx2 <= 1 + r2;
		       xx2 += 2 * r2 / steps2)
		    {
		      for (double yy2 = 1 - r2; yy2 <= 1 + r2;
			   yy2 += 2 * r2 / steps2)
			{
			  c.center_x = xxc;
			  c.center_y = yyc;
			  c.coordinate1_x = xx1;
			  c.coordinate1_y = yy1;
			  c.coordinate2_x = -yy1 * xx2;
			  c.coordinate2_y = xx1 * yy2;
			  double acc = 0;
			  bool found = true;
			  {
			    render_superpose_img render (c, scan, rparams, 255, false, false);
			    double scr_xsize =
			      render.get_width (), scr_ysize =
			      render.get_height ();
				render.precompute_all (NULL);
			    if ((scr_xsize > 600 || scr_ysize > 600) && scr_xsize < 20000)
			    {
			      found = true;
			    int tilewidth = scan.width / outertiles;
			    int tileheight = scan.height / outertiles;
			    int stepwidth = tilewidth / innertiles;
			    int stepheight = tileheight / innertiles;
			    render.precompute_all (NULL);
			    for (int x = 0; x < outertiles; x++)
			      for (int y = 0; y < outertiles; y++)
				{
				  luminosity_t red = 0, green = 0, blue = 0;
				  render.analyze_tile (x * tilewidth, y * tileheight,
						       tilewidth, tileheight,
						       stepwidth, stepheight,
						       &red, &green, &blue);
				  double sum = (red + green + blue);
				  if (sum)
				  {
				    sum = 1/sum;
				    acc +=
				      fabs (1 - red * sum) + fabs (1 - green * sum) +
				      fabs (1 - blue * sum);
				  }
				}
			    }

#if 0
			    render_fast render (c, scan, rparams, 65536);
			    double scr_xsize =
			      render.get_width (), scr_ysize =
			      render.get_height ();
				render.precompute_all ();
			    if ((scr_xsize > 600 || scr_ysize > 600) && scr_xsize < 20000)
			      {
				found = true;
				int gg=0,bb=0,rr=0;
				for (int x = 0; x < outertiles; x++)
				  for (int y = 0; y < outertiles; y++)
				    {
				      int red = 0, green = 0, blue = 0;
				      for (int sx = 0; sx < innertiles; sx++)
					for (int sy = 0; sy < innertiles;
					     sy++)
					  {
					    int r, g, b;
					    render.
					      render_pixel ((x * innertiles +
							     sx) * scr_xsize /
							    (innertiles *
							     outertiles),
							    (y * innertiles +
							     sy) * scr_ysize /
							    (innertiles *
							     outertiles), &r,
							    &g, &b);
					    red += r;
					    green += g;
					    blue += b;
					    if (r > g && r > b)
					      rr++;
					    else if (b > r && b > g)
					      bb++;
					    else if (g > r && g > b)
					      gg++;
					  }
#if 0
				      //acc += std::max (rr, std::max (gg, bb));
				      if (rr / 3 > innertiles * innertiles / 2)
					acc++;
				      if (gg / 3 > innertiles * innertiles / 2)
					acc++;
				      if (bb / 3 > innertiles * innertiles / 2)
					acc++;
#endif
#if 1
				      int avg = (red + green + blue) / 3;
				      acc +=
					abs (avg - red) + abs (avg - green) +
					abs (avg - blue);
				      //(avg - red) * (avg - red) + (avg - green) * (avg -green) + (avg - blue) * (avg-blue);
#endif
				    }
			      }
#endif
			  }
			  if (found && acc > max)
			    {
//#pragma openmp critical
			      if (acc > max)
				{
				  max = acc;
				  printf ("%f %f\n", (double)acc, (double)max);
				  best = c;
				  save_csp (stdout, &c, NULL, NULL);
				  current = c;
				  display ();
				}
			    }
			  if (steps2 <= 1)
			    break;
			}
		      if (steps2 <= 1)
			break;
		    }
		  if (stepsc <= 1)
		    break;
		}
	      if (stepsc <= 1)
		break;
	    }
	  if (steps1 <= 1)
	    break;
	}
#if 0
      if (steps1 <= 1)
	break;
#endif
    }
  current = best;
  display_scheduled = true;
  preview_display_scheduled = true;
}

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
  if (k == 'i')
    {
      std::swap (rparams.gray_min, rparams.gray_max);
      display_scheduled = true;
      preview_display_scheduled = true;
    }
  if (k == 'u')
    {
      undo_parameters ();	
      setvals ();
      display_scheduled = true;
      preview_display_scheduled = true;
    }
  if (k == 'U')
    {
      redo_parameters ();
      setvals ();
      display_scheduled = true;
      preview_display_scheduled = true;
    }
  if (k == 'm')
    {
      rparams.color_model = (render_parameters::color_model_t)((int)rparams.color_model + 1);
      if ((int)rparams.color_model >= render::num_color_models)
	rparams.color_model = (render_parameters::color_model_t)0;
      printf ("Color model: %s\n", render_parameters::color_model_names[(int)rparams.color_model]);
      display_scheduled = true;
      preview_display_scheduled = true;
    }
  if (k == 'E' && !scr_detect && scan.rgbdata)
    {
      scr_detect = true;
      display_scheduled = true;
      preview_display_scheduled = true;
    }
  if (k == 'e' && scr_detect)
    {
      scr_detect = false;
      display_scheduled = true;
      preview_display_scheduled = true;
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
      printf ("%i %i %i %i\n",pxsize, pysize,minx, maxx);
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
     }
  if (k == 'b')
    {
      rparams.backlight_temperature = std::max (rparams.backlight_temperature - 100 , (luminosity_t)render_parameters::temperature_min);
      display_scheduled = true;
      preview_display_scheduled = true;
    }
  if (k == 'B')
    {
      rparams.backlight_temperature = std::min (rparams.backlight_temperature + 100 , (luminosity_t)render_parameters::temperature_max);
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
  if (!scr_detect)
    {
      if (k == 'c')
	setcenter = true;
      if (k == 'O' && 0)
	{
static int step;
	  if (step == 0)
	    {
	      optimize (current.center_x,  current.center_y, 0, 1,  //center
			30, 0, 27, 100,  //x1
			0, 1);
	      step++;
	    }
	  else if (step == 1)
	  {
	      optimize (current.center_x,  current.center_y, (fabs (current.coordinate1_x)+fabs (current.coordinate1_y))/8,10,  //center
			current.coordinate1_x, current.coordinate1_y, 19.0*2/100, 10,  //x1
			0.003, 5);
	      step++;
	    }
	  else if (step == 2)
	    {
	      optimize (current.center_x,  current.center_y, 0, 1,  //center
			current.coordinate1_x, current.coordinate1_y, 2, 10,  //x1
			0.001, 10);
	      step++;
	    }
	}
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
      if (k == 's')
	{
	  rparams.precise = false;
	  display_scheduled = true;
	}
      if (k == 'S')
	{
	  rparams.precise = true;
	  display_scheduled = true;
	}
      if (k == 'd' && current.type != Dufay)
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
      if (k == 'f' && current.type != Finlay)
      {
	save_parameters ();
	current.type = Finlay;
	display_scheduled = true;
	preview_display_scheduled = true;
      }
      if (k >= '1' && k <='7')
	{
	  display_type = k - '1';
	  display_scheduled = true;
	}
    }
  else
    {
      if (k >= '1' && k <='7')
	{
	  scr_detect_display_type = k - '1';
	  display_scheduled = true;
	}
      if (k == 'd')
	setcolor = 1;
      if (k == 'r')
	setcolor = 2;
      if (k == 'g')
	setcolor = 3;
      if (k == 'b')
	setcolor = 4;
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

  /* Create builder and load interface */
  builder = gtk_builder_new ();
  if (!gtk_builder_add_from_file (builder,
      DATADIR "/colorscreen/barveni.glade", NULL))
    {
      fprintf (stderr, "Can not open " DATADIR "/colorscreen/barveni.glade\n");
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
  gtk_image_viewer_set_zoom_range (GTK_IMAGE_VIEWER (image_viewer), 0.1, 64);
  gtk_image_viewer_set_scale_and_shift (GTK_IMAGE_VIEWER (image_viewer), 4.0,
					4.0, 64, 64);

  // Need to do a manual zoom fit at creation because a bug when
  // not using an image.
  gtk_image_viewer_zoom_fit (GTK_IMAGE_VIEWER (image_viewer));
  data.gamma = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "gamma"));
  data.screen_blur = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "blur"));
  data.saturation = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "saturation"));
  data.presaturation = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "presaturation"));
  data.y2 = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "y2"));
  data.brightness = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "brightness"));
  data.k1 = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "k1"));
  data.tilt_x_x = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "tilt_x_x"));
  data.tilt_x_y = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "tilt_x_y"));
  data.tilt_y_x = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "tilt_y_x"));
  data.tilt_y_y = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "tilt_y_y"));
  data.mix_gamma = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "mix_gamma"));
  data.mix_red = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "mix_red"));
  data.mix_green = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "mix_green"));
  data.mix_blue = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "mix_blue"));
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
  return current;
}

static inline void
init_transformation_data (scr_to_img *trans)
{
  trans->set_parameters (get_scr_to_img_parameters (), scan);
}

static inline void
my_putpixel (guchar * pixels, int rowstride, int x, int y, int r, int g,
	     int b)
{
  *(pixels + y * rowstride + x * 3) = r;
  *(pixels + y * rowstride + x * 3 + 1) = g;
  *(pixels + y * rowstride + x * 3 + 2) = b;
}

/* This renders the small preview widget.   */

static void
previewrender (GdkPixbuf ** pixbuf)
{
  guint8 *pixels;
  render_fast render (get_scr_to_img_parameters (), scan, rparams, 255);
  int scr_xsize = render.get_width (), scr_ysize = render.get_height (), rowstride;
  int max_size = std::max (scr_xsize, scr_ysize);
  coord_t step = max_size / (coord_t)PREVIEWSIZE;
  int my_xsize = ceil (scr_xsize / step), my_ysize = ceil (scr_ysize / step);
  if (!render.precompute_all (NULL))
    return;

  g_object_unref (*pixbuf);
  *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, 0, 8, my_xsize, my_ysize);

  pixels = gdk_pixbuf_get_pixels (*pixbuf);
  rowstride = gdk_pixbuf_get_rowstride (*pixbuf);
#pragma omp parallel for default(none) shared(render,pixels,step,my_ysize,my_xsize,rowstride)
  for (int y = 0; y < my_ysize; y ++)
    for (int x = 0; x < my_xsize; x ++)
      {
	int red, green, blue;
	render.render_pixel (x * step, y * step, &red, &green, &blue);
	my_putpixel (pixels, rowstride, x, y, red, green, blue);
      }
}

static void
draw_circle (cairo_surface_t *surface, coord_t bigscale,
    	     int xoffset, int yoffset,
    	     coord_t x, coord_t y, coord_t r, coord_t g, coord_t b)
{
  cairo_t *cr = cairo_create (surface);
  cairo_translate (cr, -xoffset, -yoffset);
  cairo_scale (cr, bigscale, bigscale);

  cairo_set_source_rgba (cr, r, g, b, 0.5);
  cairo_arc (cr, x, y, 3, 0.0, 2 * G_PI);

  cairo_fill (cr);
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
    if (scr_detect)
      {
	ret = render_scr_detect::render_tile ((enum render_scr_detect::render_scr_detect_type_t)scr_detect_display_type, current_scr_detect, scan, rparams, color_display,
				    bigpixels, 4, bigrowstride, pxsize, pysize, xoffset, yoffset, step, &progress);
      }
    else
      ret = render_to_scr::render_tile ((enum render_to_scr::render_type_t)display_type, get_scr_to_img_parameters (), scan, rparams, color_display,
				  bigpixels, 4, bigrowstride, pxsize, pysize, xoffset, yoffset, step, &progress);
  }

  cairo_surface_t *surface
    = cairo_image_surface_create_for_data (bigpixels,
					   CAIRO_FORMAT_RGB24,
					   pxsize,
					   pysize,
					   bigrowstride);
  draw_circle (surface, bigscale, xoffset, yoffset, current.center_x, current.center_y, 0, 0, 1);
  draw_circle (surface, bigscale, xoffset, yoffset, current.center_x + current.coordinate1_x, current.center_y + current.coordinate1_y, 1, 0, 0);
  draw_circle (surface, bigscale, xoffset, yoffset, current.center_x + current.coordinate2_x, current.center_y + current.coordinate2_y, 0, 1, 0);

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
  if (scr_detect)
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
  else
    {
      if (event->button == 1 && setcenter)
	{
	  double newcenter_x;
	  double newcenter_y;
	  newcenter_x = (event->x + shift_x) / scale_x;
	  newcenter_y = (event->y + shift_y) / scale_y;
	  if (newcenter_x != current.center_x || newcenter_y != current.center_y)
	    {
	      current.center_x = newcenter_x;
	      current.center_y = newcenter_y;
	      setcenter = false;
	      setvals ();
	      display_scheduled = true;
	      preview_display_scheduled = true;
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
  if (scr_detect)
    return;
  gdouble scale_x, scale_y;
  gint shift_x, shift_y;
  gtk_image_viewer_get_scale_and_shift (GTK_IMAGE_VIEWER
					(data.image_viewer), &scale_x,
					&scale_y, &shift_x, &shift_y);
  if (button == 1)
    {
      double xoffset = (x - xpress1) / scale_x;
      double yoffset = (y - ypress1) / scale_y;
      if (current.center_x == press_parameters.center_x + xoffset
          && current.center_y == press_parameters.center_y + yoffset)
	return;
      current.center_x = press_parameters.center_x + xoffset;
      current.center_y = press_parameters.center_y + yoffset;
      setvals ();
      display_scheduled = true;
      preview_display_scheduled = true;
    }
  else if (button == 3)
    {
      double x1 = (xpress - current.center_x);
      double y1 = (ypress - current.center_y);
      double x2 = (x + shift_x) / scale_x - current.center_x;
      double y2 = (y + shift_y) / scale_y - current.center_y;
      double scale = sqrt ((x2 * x2) + (y2 * y2))/sqrt ((x1*x1) + (y1*y1));
      double angle = atan2f (y2, x2) - atan2f (y1, x1);
      if (!angle)
	return;
      if (!freeze_x)
	{
	  current.coordinate1_x = (press_parameters.coordinate1_x * cos (angle)
				  + press_parameters.coordinate1_y * sin (angle)) * scale;
	  current.coordinate1_y = (press_parameters.coordinate1_x * sin (angle)
				  + press_parameters.coordinate1_y * cos (angle)) * scale;
	}
      if (!freeze_y)
	{
	  current.coordinate2_x = (press_parameters.coordinate2_x * cos (-angle)
				  + press_parameters.coordinate2_y * sin (-angle)) * scale;
	  current.coordinate2_y = (press_parameters.coordinate2_x * sin (-angle)
				  + press_parameters.coordinate2_y * cos (-angle)) * scale;
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
  out = fopen (paroname, "w");
  if (!out)
    {
      perror (paroname);
    }
  if (!save_csp (out, &current, scan.rgbdata ? &current_scr_detect : NULL, &rparams))
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
  if (argc != 4 && argc != 5)
    {
      fprintf (stderr, "Invocation: %s scan.pgm output.pnm scan.par [scan-rgb.pnm]\n\n"
	       "scan.pgm is the scan as a greyscale.\n"
	       "output.pnm is a filename where resulting image will be stored.\n"
	       "If scan.par exists then its parametrs will be read.\n"
	       "Parameters will be saved to scan.par after pressing save button.\n"
	       "scan-rgb.pnm is an optional RGB scan of the same original.\n",
	       argv[0]);
      exit (1);
    }
  openimage (&argc, argv);
  oname = argv[2];
  paroname = argv[3];

  FILE *in = fopen (paroname, "rt");
  const char *error;
  if (in && !load_csp (in, &current, &current_scr_detect, &rparams, &error))
    fprintf (stderr, "%s\n", error);
  if (in)
    fclose (in);
  else
    {
      fprintf (stderr, "Can not open param file \"%s\": ", paroname);
      perror ("");
    }
  save_csp (stdout, &current, scan.rgbdata ? &current_scr_detect : NULL, &rparams);
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
