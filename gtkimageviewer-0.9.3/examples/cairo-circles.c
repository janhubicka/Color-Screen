//======================================================================
//  cairo-circles.c - An example of how to use cairo to draw on demand.
//
//  This program is released in the public domain. 
//
//  Dov Grobgeld <dov.grobgeld@gmail.com>
//  Fri Oct  3 15:51:01 2008
//----------------------------------------------------------------------
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <cairo.h>
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
  int img_width = gdk_pixbuf_get_width(pixbuf);
  int img_height = gdk_pixbuf_get_height(pixbuf);

  cairo_surface_t *surface
    = cairo_image_surface_create_for_data(gdk_pixbuf_get_pixels(pixbuf),
                                          CAIRO_FORMAT_RGB24,
                                          img_width,
                                          img_height,
                                          gdk_pixbuf_get_rowstride(pixbuf));
  cairo_t *cr = cairo_create (surface);
  cairo_translate(cr, -shift_x, -shift_y);
  cairo_scale(cr, scale_x, scale_y);

  // Now do any cairo commands you want, but you have to swap
  // R and B in set_source_rgba() commands because of cairo and
  // pixbuf incompabilitities.
  cairo_set_source_rgba (cr, 0,0,1.0,0.5);
  cairo_arc(cr,
            -1, 0,
            3, 0.0, 2*G_PI);
  cairo_fill(cr);

  cairo_set_source_rgba (cr, 1.0,0,0,0.5);
  cairo_arc(cr,
            1, 0,
            3, 0.0, 2*G_PI);
  cairo_fill(cr);

  cairo_set_source_rgba (cr, 0,0.9,0,0.9);
  cairo_arc(cr,
            0, 0,
            1, 0.0, 2*G_PI);
  cairo_fill(cr);

  // Display some tiny text
  cairo_set_font_size(cr, 0.08);
  cairo_set_source_rgba (cr, 0,0,0,1);
  cairo_move_to(cr, -0.2, 0);
  cairo_show_text(cr, "Cairo Rules!!!");
    
  // cleanup
  cairo_surface_destroy(surface);
  cairo_destroy(cr);
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
  gtk_window_set_title (GTK_WINDOW (window), "cairo-circles");
  g_signal_connect (window, "destroy", G_CALLBACK (gtk_exit), NULL);

  scrolled_win = gtk_scrolled_window_new(NULL,NULL);
  gtk_container_add (GTK_CONTAINER (window), scrolled_win);
  gtk_widget_show(scrolled_win);
    
  image_viewer = gtk_image_viewer_new(NULL);
  g_signal_connect(image_viewer,
                   "image-annotate",
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
