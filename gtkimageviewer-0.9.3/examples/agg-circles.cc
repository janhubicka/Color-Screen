//======================================================================
//  agg-example.c - Drawing with agg on the gtk image viewer.
//
//  This program is in the public domain
//  Dov Grobgeld <dov.grobgeld@gmail.com>
//  Sun Oct  5 21:34:03 2008
//----------------------------------------------------------------------

#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "agg2/agg_rasterizer_scanline_aa.h"
#include "agg2/agg_ellipse.h"
#include "agg2/agg_scanline_p.h"
#include "agg2/agg_renderer_scanline.h"
#include "agg2/agg_path_storage.h"
#include "agg2/agg_pixfmt_rgba.h"
#include "gtk-image-viewer.h"

static void
cb_image_annotate(GtkImageViewer *imgv,
                  GdkPixbuf *pixbuf,
                  gint shift_x,
                  gint shift_y,
                  gdouble scale_x,
                  gdouble scale_y,
                  gpointer user_data
                  )
{
  int width = gdk_pixbuf_get_width(pixbuf);
  int height = gdk_pixbuf_get_height(pixbuf);

  // initialize agg
  agg::rendering_buffer rbuf(gdk_pixbuf_get_pixels(pixbuf), 
                             width, 
                             height,
                             gdk_pixbuf_get_rowstride(pixbuf));
  agg::pixfmt_rgba32 pixf(rbuf);
  agg::renderer_base<agg::pixfmt_rgba32> rbase(pixf);
  agg::rasterizer_scanline_aa<> pf;
  agg::scanline_p8 sl;
  agg::ellipse e1;

  // Setup the affine transform
  agg::trans_affine mtx;
  mtx *= agg::trans_affine_scaling(scale_x, scale_y);
  mtx *= agg::trans_affine_translation(-shift_x, -shift_y);

  // Apply it for each element being drawn.
  agg::conv_transform<agg::ellipse, agg::trans_affine> trans(e1, mtx);

  e1.init(1,0,3,3,128);
  pf.add_path(trans);
  agg::render_scanlines_aa_solid(pf, sl, rbase,
                                 agg::rgba(1,0,0,0.5));

  e1.init(-1,0,3,3,128);
  pf.add_path(trans);
  agg::render_scanlines_aa_solid(pf, sl, rbase,
                                 agg::rgba(0,0,1,0.5));

  e1.init(0,0,1,1,128);
  pf.add_path(trans);
  agg::render_scanlines_aa_solid(pf, sl, rbase,
                                 agg::rgba(0,0.9,0,0.9));

}

static gint
cb_key_press_event(GtkWidget *widget, GdkEventKey *event)
{
  gint k = event->keyval;
  
  if (k == 'q')
    exit(0);

  return FALSE;
}

int main(int argc, char **argv)
{
  GtkWidget *window, *image_viewer, *scrolled_win;

  gtk_init (&argc, &argv);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_policy(GTK_WINDOW(window), TRUE, TRUE, FALSE);
    
  gtk_window_set_title (GTK_WINDOW (window), "Agg rendering demo");
    
  g_signal_connect (window, "destroy",
                    G_CALLBACK (gtk_exit), NULL);

  scrolled_win = gtk_scrolled_window_new(NULL,NULL);
  gtk_container_add (GTK_CONTAINER (window), scrolled_win);
  gtk_widget_show(scrolled_win);
    
  image_viewer = gtk_image_viewer_new(NULL);
  g_signal_connect(image_viewer, "image-annotate",
                   G_CALLBACK(cb_image_annotate), NULL);
  gtk_widget_set_size_request (window, 500,500);

  gtk_signal_connect (GTK_OBJECT(window),     "key_press_event",
                      GTK_SIGNAL_FUNC(cb_key_press_event), NULL);
    
  gtk_container_add (GTK_CONTAINER (scrolled_win), image_viewer);
    
  gtk_widget_show (image_viewer);
  gtk_widget_show (window);

  // Set the scroll region and zoom range
  gtk_image_viewer_set_scroll_region(GTK_IMAGE_VIEWER(image_viewer),
                                     -5,-5,5,5);
  gtk_image_viewer_set_zoom_range(GTK_IMAGE_VIEWER(image_viewer),
                                  -HUGE, HUGE);

  // Need to do a manual zoom fit at creation because a bug when
  // not using an image.
  gtk_image_viewer_zoom_fit(GTK_IMAGE_VIEWER(image_viewer));
  gtk_main ();

  exit(0);
  return(0);
}

