#include <math.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h> /* for pdf specifics and forms */
#include <mupdf/ucdn.h>

#include "from-webkit.h"
#include "paper-gtk.h"
#include <gtk/gtk.h>

static fz_context *ctx;

void ensure_chapter_is_loaded(DocInfo *doci, int chapter) {
  if (doci->pages[chapter])
    return;
  doci->page_count_for_chapter[chapter] =
      fz_count_chapter_pages(ctx, doci->doc, chapter);
  doci->pages[chapter] =
      calloc(sizeof(Page), doci->page_count_for_chapter[chapter]);
}

void ensure_page_is_loaded(DocInfo *doci, fz_location location) {
  ensure_chapter_is_loaded(doci, location.chapter);
  Page *page = &doci->pages[location.chapter][location.page];
  if (page->page)
    return;
  page->page =
      fz_load_chapter_page(ctx, doci->doc, location.chapter, location.page);
  page->page_text = fz_new_stext_page_from_page(ctx, page->page, NULL);
  page->seps = NULL; // TODO seps
  page->links = fz_load_links(ctx, page->page);
  page->page_bounds = fz_bound_page(ctx, page->page);
  page->display_list = fz_new_display_list(ctx, page->page_bounds);
  // populate display_list
  fz_device *device = fz_new_list_device(ctx, page->display_list);
  fz_run_page(ctx, page->page, device, fz_identity, NULL);
  fz_close_device(ctx, device);
  fz_drop_device(ctx, device);
}

Page *get_page(DocInfo *doci, fz_location loc) {
  ensure_page_is_loaded(doci, loc);
  return &doci->pages[loc.chapter][loc.page];
}

fz_matrix get_scale_ctm(DocInfo *doci, Page *page) {
  return fz_transform_page(page->page_bounds, doci->zoom, doci->rotate);
}

/*
 * Set scroll_x so the current page is centered.
 */
static void center_page(int surface_width, DocInfo *doci) {
  Page *page = get_page(doci, doci->location);
  fz_matrix scale_ctm = get_scale_ctm(doci, page);
  fz_irect scaled_bounds =
      fz_round_rect(fz_transform_rect(page->page_bounds, scale_ctm));
  doci->scroll_x =
      ((double)(surface_width - (int)fz_irect_width(scaled_bounds))) / 2;
}

gboolean draw_callback(GtkWidget *widget, cairo_t *cr, Client *c) {

  cairo_surface_t *surface = c->image_surf;

  unsigned int width = cairo_image_surface_get_width(surface);
  unsigned int height = cairo_image_surface_get_height(surface);

  unsigned char *image = cairo_image_surface_get_data(surface);

  fz_irect whole_rect = {.x1 = width, .y1 = height};

  if (c->has_mouse_event && c->mouse_event.button == 2) {
    center_page(width, c->doci);
    fprintf(stderr, "centering!\n");
  }

  fz_pixmap *pixmap = fz_new_pixmap_with_bbox_and_data(
      ctx, c->doci->colorspace, whole_rect, NULL, 1, image);
  // background
  fz_clear_pixmap_with_value(ctx, pixmap, 0xF0);

  fz_device *draw_device = fz_new_draw_device(ctx, fz_identity, pixmap);
  fz_location loc = c->doci->location;
  fz_try(ctx) { ensure_page_is_loaded(c->doci, loc); }
  fz_catch(ctx) {
    fprintf(stderr, "can't load page");
    exit(EXIT_FAILURE);
  }

  Page *page = &c->doci->pages[loc.chapter][loc.page];
  fz_matrix scale_ctm = get_scale_ctm(c->doci, page);
  float stopped_y =
      fz_transform_point(fz_make_point(0, -c->doci->scroll_y), scale_ctm).y;
  while (stopped_y < height) {
    fz_matrix scale_ctm = get_scale_ctm(c->doci, page);
    fz_matrix draw_page_ctm =
        fz_concat(scale_ctm, fz_translate(c->doci->scroll_x, stopped_y));
    fz_clear_pixmap_rect_with_value(
        ctx, pixmap, 0xFF,
        fz_round_rect(fz_transform_rect(page->page_bounds, draw_page_ctm)));
    /* fz_run_page(ctx, page->page, draw_device, draw_page_ctm, &cookie); */
    fz_run_display_list(ctx, page->display_list, draw_device, draw_page_ctm,
                        page->page_bounds, NULL);
    int margin = 20;
    stopped_y += fz_transform_rect(page->page_bounds, scale_ctm).y1 + margin;
    fprintf(stderr, "\rscroll_y: %3.0f, stopped_y: %3.0f", c->doci->scroll_y,
            stopped_y);
    fz_location next = fz_next_page(ctx, c->doci->doc, loc);
    if (next.chapter == loc.chapter && next.page == loc.page) {
      // end of document
      break;
    } else {
      loc = next;
    }
    page = get_page(c->doci, loc);
  }

  fz_close_device(ctx, draw_device);
  fz_drop_device(ctx, draw_device);
  fz_drop_pixmap(ctx, pixmap);
  cairo_set_source_surface(cr, surface, 0, 0);
  cairo_paint(cr);

  if (c->has_mouse_event) {
    c->has_mouse_event = FALSE;
    // draw a circle where clicked
    GdkRGBA color;
    GtkStyleContext *style = gtk_widget_get_style_context(widget);
    gtk_render_background(style, cr, 0, 0, width, height);
    cairo_arc(cr, c->mouse_event.x, c->mouse_event.y, 20.0, 0, 2 * G_PI);
    gtk_style_context_get_color(style, gtk_style_context_get_state(style),
                                &color);
    gdk_cairo_set_source_rgba(cr, &color);
    cairo_fill(cr);
  }

  return FALSE;
}

static void allocate_pixmap(GtkWidget *widget, GdkRectangle *allocation,
                            Client *c) {
  cairo_surface_destroy(c->image_surf);
  c->image_surf = cairo_image_surface_create(
      CAIRO_FORMAT_RGB24, allocation->width, allocation->height);
}

static gboolean button_press_event(GtkWidget *widget, GdkEventButton *event,
                                   Client *c) {
  c->mouse_event = *event;
  c->has_mouse_event = TRUE;
  fprintf(stderr, "Mouse button: %d, type: %d\n", event->button, event->type);
  gtk_widget_queue_draw(widget);
  return FALSE;
}

static void scroll(DocInfo *doci, float delta_x, float delta_y) {
  // TODO don't let scroll_x get out of the page
  doci->scroll_x += delta_x;
  doci->scroll_y += delta_y;
  // move to next pages if scroll_y is past the page bound
  while (doci->scroll_y >= get_page(doci, doci->location)->page_bounds.y1) {
    doci->scroll_y -= get_page(doci, doci->location)->page_bounds.y1;
    doci->location = fz_next_page(ctx, doci->doc, doci->location);
  }
  // move to previous pages if scroll_y is negative
  while (doci->scroll_y < 0) {
    doci->location = fz_previous_page(ctx, doci->doc, doci->location);
    doci->scroll_y += get_page(doci, doci->location)->page_bounds.y1;
  }
}

static gboolean scroll_event(GtkWidget *widget, GdkEventScroll *event,
                             Client *c) {
  if (event->type != GDK_SCROLL) {
    fprintf(stderr, "Scroll handler called on something that isn't scroll.\n");
    return TRUE;
  }
  float d_x = 0.0f, d_y = 0.0f;
  if (event->state & GDK_CONTROL_MASK) { // zoom
    switch (event->direction) {
    case GDK_SCROLL_UP:
      d_y = 10;
      break;
    case GDK_SCROLL_DOWN:
      d_y = -10;
      break;
    default:
      fprintf(stderr, "unhandled zoom scroll case\n");
    }
    c->doci->zoom += d_y;
  } else { // scroll
    // TODO check state and zoom for ctrl+scroll
    switch (event->direction) {
    case GDK_SCROLL_UP:
      d_y = -50;
      break;
    case GDK_SCROLL_DOWN:
      d_y = 50;
      break;
    case GDK_SCROLL_LEFT:
      d_x = -50;
      break;
    case GDK_SCROLL_RIGHT:
      d_x = 50;
      break;
    case GDK_SCROLL_SMOOTH:
      d_x = event->delta_x;
      d_y = event->delta_y;
      fprintf(stderr, "Smooth scroll\n");
      break;
    }
    scroll(c->doci, d_x, d_y);
  }
  gtk_widget_queue_draw(widget);
  return FALSE;
}

static void activate(GtkApplication *app, gpointer user_data) {
  GtkWidget *window;

  window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "Window");
  gtk_window_set_default_size(GTK_WINDOW(window), 900, 900);
  Client *c = (Client *)user_data;

  c->container = gtk_drawing_area_new();

  gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(c->container));

  g_signal_connect(G_OBJECT(c->container), "draw", G_CALLBACK(draw_callback),
                   c);
  g_signal_connect(G_OBJECT(c->container), "size-allocate",
                   G_CALLBACK(allocate_pixmap), c);
  // handle mouse hover and click

  // TODO when I also add GDK_SMOOTH_SCROLL_MASK all scroll events turn to
  // smooth ones with deltas of 0, I don't know how to find the direction in
  // those cases
  gtk_widget_add_events(c->container,
                        GDK_EXPOSURE_MASK | GDK_LEAVE_NOTIFY_MASK |
                            GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                            GDK_BUTTON2_MASK | GDK_BUTTON3_MASK |
                            GDK_POINTER_MOTION_MASK |
                            GDK_POINTER_MOTION_HINT_MASK | GDK_SCROLL_MASK);
  /* gtk_signal_connect(GTK_OBJECT(c->container), "motion_notify_event", */
  /*                    (GtkSignalFunc)motion_notify_event, c); */
  g_signal_connect(G_OBJECT(c->container), "button-release-event",
                   G_CALLBACK(button_press_event), c);
  g_signal_connect(G_OBJECT(c->container), "scroll-event",
                   G_CALLBACK(scroll_event), c);

  gtk_widget_show_all(window);
}

void load_doc(DocInfo *doci, char *filename, char *accel_filename) {
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
  fz_location loc = {0, 0};
  doci->location = loc;
  doci->colorspace = fz_device_rgb(ctx);
  doci->zoom = 100.0f;
  /* Count the number of pages. */
  fz_try(ctx) {
    doci->chapter_count = fz_count_chapters(ctx, doci->doc);
    if (!(doci->pages = calloc(sizeof(Page *), doci->chapter_count))) {
      fz_throw(ctx, 1, "Can't allocate");
    }
    if (!(doci->page_count_for_chapter =
              calloc(sizeof(int *), doci->chapter_count))) {
      fz_throw(ctx, 1, "Can't allocate");
    }
  }
  fz_catch(ctx) {
    fprintf(stderr, "cannot count number of pages: %s\n",
            fz_caught_message(ctx));
    fz_drop_document(ctx, doci->doc);
    fz_drop_context(ctx);
    exit(EXIT_FAILURE);
  }
}

void drop_page(Page *page) {
  fz_drop_stext_page(ctx, page->page_text);
  fz_drop_separations(ctx, page->seps);
  fz_drop_link(ctx, page->links);
  fz_drop_page(ctx, page->page);
  fz_drop_display_list(ctx, page->display_list);
  memset(page, 0, sizeof(*page));
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
  load_doc(doci, "./amsmath.pdf", NULL);
  fz_location loc = {0, 1};
  doci->location = loc;
  doci->zoom = 50.0f;
  /* doci->scroll_y = -get_page(doci, doci->location)->page_bounds.y1 / 2; */
  fprintf(stderr, "bounds: w %f, h %f\n",
          get_page(doci, doci->location)->page_bounds.x1,
          get_page(doci, doci->location)->page_bounds.y1);
  app = gtk_application_new("org.gtk.example", G_APPLICATION_FLAGS_NONE);
  Client client;
  Client *c = &client;
  c->doci = doci;
  g_signal_connect(app, "activate", G_CALLBACK(activate), c);
  status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);

  return status;
}
