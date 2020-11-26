#include <mupdf/fitz.h>
#include <mupdf/pdf.h> /* for pdf specifics and forms */
#include <mupdf/ucdn.h>

#include <gtk/gtk.h>
#include "mupdf-gtk.h"
#include "from-webkit.h"

gboolean
draw_callback (GtkWidget *widget, cairo_t *cr, gpointer data)
{
  guint width, height;
  GtkStyleContext *style;

  style = gtk_widget_get_style_context(widget);

  width = gtk_widget_get_allocated_width(widget);
  height = gtk_widget_get_allocated_height(widget);

  gtk_render_background(style, cr, 0, 0, width, height);

  /* cairo_arc(cr, width / 2.0, height / 2.0, MIN(width, height) / 2.0, 0, */
  /*           2 * G_PI); */

  /* gtk_style_context_get_color(style, gtk_style_context_get_state(style), */
  /*                             &color); */
  /* gdk_cairo_set_source_rgba(cr, &color); */

  /* cairo_fill(cr); */
  cairo_surface_t *sur =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  {
    cairo_t *crr = cairo_create(sur);
    cairo_set_line_width(crr, 0.1);
    cairo_set_source_rgb(crr, 0.2, 0.5, 0);
    cairo_rectangle(crr, 0.25, 0.25, 20, 20);
    cairo_fill(crr);
    cairo_stroke(crr);
    cairo_destroy(crr);
  }

  cairo_set_source_surface(cr, sur, 0, 0);
  cairo_paint(cr);

 return FALSE;
}

static void activate(GtkApplication *app, gpointer user_data) {
  GtkWidget *window;

  window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "Window");
  gtk_window_set_default_size(GTK_WINDOW(window), 200, 200);
  Client *c = (Client *)user_data;


  c->container = gtk_drawing_area_new();

  gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(c->container));

  g_signal_connect(G_OBJECT(c->container), "draw", G_CALLBACK(draw_callback),
                   c);

  gtk_widget_show_all(window);
}

int main(int argc, char **argv) {
  GtkApplication *app;
  int status;

  app = gtk_application_new("org.gtk.example", G_APPLICATION_FLAGS_NONE);
  Client client;
  Client *c = &client;
  g_signal_connect(app, "activate", G_CALLBACK(activate), c);
  status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);

  return status;
}
