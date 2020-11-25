#include <mupdf/fitz.h>
#include <mupdf/pdf.h> /* for pdf specifics and forms */
#include <mupdf/ucdn.h>

#include <gtk/gtk.h>
#include "mupdf-gtk.h"
#include "from-webkit.h"

static fz_context *ctx;

/*
 * Update draw_page_ctm and draw_page_bounds from page_bounds and rotate.
 */
void transform_page(DocInfo *doci) {
  doci->draw_page_ctm =
      fz_transform_page(doci->page_bounds, doci->zoom, doci->rotate);
  doci->draw_page_bounds =
      fz_transform_rect(doci->page_bounds, doci->draw_page_ctm);
}

gboolean draw_callback(GtkWidget *widget, cairo_t *cr, gpointer data) {
  Client *c = (Client *)data;

  GdkPixbuf *pixbuf = gtk_image_get_pixbuf((GtkImage *)widget);

  unsigned int width = gdk_pixbuf_get_width(pixbuf);
  unsigned int height = gdk_pixbuf_get_height(pixbuf);

  fprintf(stderr, "w: %d, h: %d\n", width, height);
  fz_irect irect = {.x1 = width, .y1 = height};
  fz_rect rect = {.x1 = width, .y1 = height};

  fz_display_list *display_list = fz_new_display_list(ctx, rect);
  fz_device *list_device = fz_new_list_device(ctx, display_list);

  /* fz_try(ctx) { */
  transform_page(c->doci);
  fz_run_page(ctx, c->doci->page, list_device, c->doci->draw_page_ctm, NULL);
  /* } */
  /* fz_catch(ctx) { return ZATHURA_ERROR_UNKNOWN; } */

  fz_close_device(ctx, list_device);
  fz_drop_device(ctx, list_device);

  unsigned char *image = gdk_pixbuf_get_pixels(pixbuf);

  fz_pixmap *pixmap = fz_new_pixmap_with_bbox_and_data(
      ctx, c->doci->colorspace, irect, c->doci->seps, 1, image);
  fz_clear_pixmap_with_value(ctx, pixmap, 0xFF);

  fz_device *draw_device = fz_new_draw_device(ctx, fz_identity, pixmap);
  fz_run_display_list(ctx, display_list, draw_device, fz_identity, rect, NULL);
  fz_close_device(ctx, draw_device);
  fz_drop_device(ctx, draw_device);

  fz_drop_pixmap(ctx, pixmap);
  fz_drop_display_list(ctx, display_list);

  return FALSE;
}

static void activate(GtkApplication *app, gpointer user_data) {
  GtkWidget *window;

  window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "Window");
  gtk_window_set_default_size(GTK_WINDOW(window), 200, 200);
  Client *c = (Client *)user_data;

  c->container = gtk_image_new_from_pixbuf(gdk_pixbuf_new(
      GDK_COLORSPACE_RGB, TRUE, 24, 200, 200));

  gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(c->container));

  g_signal_connect(G_OBJECT(c->container), "draw", G_CALLBACK(draw_callback),
                   c);
  gtk_widget_show_all(window);
}

void init_doc(DocInfo *doci, char *filename, char *accel_filename) {
  // zero it all out - the short way of setting everything to NULL.
  memset(doci, 0, sizeof(*doci));
  strcpy(doci->filename, filename);
  if (accel_filename)
    strcpy(doci->accel, accel_filename);

  fz_try(ctx) doci->doc =
      fz_open_accelerated_document(ctx, doci->filename, doci->accel);
  fz_catch(ctx) {
    fprintf(stderr, "cannot open document: %s\n", fz_caught_message(ctx));
    fz_drop_context(ctx);
    exit(EXIT_FAILURE);
  }

  /* Count the number of pages. */
  fz_try(ctx) { doci->page_count = fz_count_pages(ctx, doci->doc); }
  fz_catch(ctx) {
    fprintf(stderr, "cannot count number of pages: %s\n",
            fz_caught_message(ctx));
    fz_drop_document(ctx, doci->doc);
    fz_drop_context(ctx);
    exit(EXIT_FAILURE);
  }
  fz_location loc = {0, 0};
  doci->location = loc;
  doci->colorspace = fz_device_rgb(ctx);
}

void load_page(DocInfo *doci, fz_location location) {

  fz_drop_stext_page(ctx, doci->page_text);
  doci->page_text = NULL;
  fz_drop_separations(ctx, doci->seps);
  doci->seps = NULL;
  fz_drop_link(ctx, doci->links);
  doci->links = NULL;
  fz_drop_page(ctx, doci->page);
  doci->page = NULL;
  doci->location = location;

  doci->page = fz_load_chapter_page(ctx, doci->doc, doci->location.chapter,
                                    doci->location.page);
}

int main(int argc, char **argv) {
  ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
  GtkApplication *app;
  int status;

  fz_try(ctx) { fz_register_document_handlers(ctx); }
  fz_catch(ctx) {
    fprintf(stderr, "cannot register document handlers: %s\n",
            fz_caught_message(ctx));
    fz_drop_context(ctx);
    return EXIT_FAILURE;
  }

  DocInfo _doci;
  DocInfo *doci = &_doci;
  // TODO accel logic
  init_doc(doci, "./cancel.pdf", NULL);
  fz_location loc = {0, 0};
  load_page(doci, loc);

  app = gtk_application_new("org.gtk.example", G_APPLICATION_FLAGS_NONE);
  Client client;
  Client *c = &client;
  c->doci = doci;
  g_signal_connect(app, "activate", G_CALLBACK(activate), c);
  status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);

  return status;
}
